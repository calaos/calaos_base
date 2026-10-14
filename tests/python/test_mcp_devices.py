# Devices through the MCP tools (spec 9.2, decision 18): the protected outputs of
# a thermostat are hidden and refused, a device takes its JSON command, and the
# set_io_state description is rendered from IODoc.
import asyncio
import copy
import json
import os
import sys
import types

import pytest

if "websockets" not in sys.modules:
    try:
        import websockets  # noqa: F401
    except ImportError:
        sys.modules["websockets"] = types.ModuleType("websockets")

from calaos_mcp import iodoc, safety  # noqa: E402
from calaos_mcp.client import CalaosClient  # noqa: E402
from calaos_mcp.tools import io, rooms  # noqa: E402
from calaos_mcp.tools._home import protected_outputs  # noqa: E402


def run(coro):
    return asyncio.run(coro)


def item(io_id, **kw):
    base = {"id": io_id, "name": io_id, "gui_type": "light", "var_type": "bool",
            "io_type": "output", "state": "false", "visible": "true"}
    base.update(kw)
    return base


def thermostat(io_id="salon_thermo", with_cool=True, impl="thermostat", **kw):
    ios = {"current_temp": "salon_temp", "target_temp": "salon_target",
           "output_heat": "relay_heat"}
    modes = ["off", "heat"]
    if with_cool:
        ios["output_cool"] = "relay_cool"
        ios["target_temp_low"] = "salon_low"
        ios["target_temp_high"] = "salon_high"
        modes = ["off", "heat", "cool", "auto"]
    caps = {"modes": modes,
            "target_temp": {"min": 7, "max": 35, "step": 0.5, "unit": "C"}}
    if with_cool:
        caps["target_temp_range"] = True
    return item(io_id, gui_type="climate", var_type="string", io_type="inout",
                state="heat", device={
                    "family": "climate", "impl": impl, "capabilities": caps,
                    "state": {"mode": "heat", "current_temp": 19.5,
                              "target_temp": 21.0, "action": "heating"},
                    "ios": ios}, **kw)


def make_home(**thermo):
    return {"home": [
        {"name": "Salon", "type": "salon", "items": [
            item("light1"),
            thermostat(**thermo),
            item("relay_heat"),
            item("relay_cool"),
            item("relay_other"),
            item("salon_target", var_type="float", gui_type="var_int",
                 io_type="inout", visible="false", state="21"),
        ]},
        {"name": "Chambre", "type": "chambre", "items": [
            item("ac", gui_type="climate", var_type="string", io_type="inout",
                 state="cool", device={
                     "family": "climate", "impl": "proxy",
                     "capabilities": {"modes": ["off", "cool"],
                                      "fan_modes": ["auto", "low"]},
                     "state": {"mode": "cool"},
                     "ios": {"mode": "ac_mode", "output_heat": "relay_ac"}}),
            item("ac_mode", gui_type="var_string", var_type="string",
                 io_type="inout"),
            item("relay_ac"),
        ]},
    ]}


class FakeClient:
    def __init__(self, home, fresh=None):
        self.home = home
        self.fresh = fresh
        self.sent = []
        self.forced = []

    async def get_home(self, force=False):
        self.forced.append(force)
        return self.fresh if force and self.fresh is not None else self.home

    async def get_state(self, ids):
        return {i: "heat" for i in ids}

    async def set_state(self, io_id, value):
        self.sent.append((io_id, value))
        return {"success": "true"}


def ids(entries):
    return [e["id"] for e in entries]


DEVICE_DOC = {
    "command_format": "json",
    "family": "climate",
    "description": "A thermostat.",
    "command_fields": [
        {"name": "mode", "type": "enum", "values": ["off", "heat", "cool", "auto"],
         "description": "Operating mode.", "requires_links": [],
         "value_requires_links": {"cool": ["output_cool"]}},
        {"name": "target_temp", "type": "number", "min": 7.0, "max": 35.0,
         "step": 0.5, "unit": "°C", "description": "The setpoint.",
         "requires_links": ["target_temp"]},
        {"name": "fan_mode", "type": "enum", "values_param": "fan_modes",
         "description": "Fan.", "requires_links": ["fan_mode"]},
    ],
}


@pytest.fixture
def iodoc_file(tmp_path, monkeypatch):
    def write(devices):
        p = tmp_path / "io_doc.json"
        p.write_text(json.dumps({"light": {}, "devices": devices}))
        monkeypatch.setenv(iodoc.ENV_PATH, str(p))
        return p
    return write


# --- masking ---------------------------------------------------------------

def test_protected_outputs_come_from_the_thermostat_links():
    assert protected_outputs(make_home()) == {
        "relay_heat": ("salon_thermo", "output_heat"),
        "relay_cool": ("salon_thermo", "output_cool"),
    }


def test_a_thermostat_without_cool_protects_only_the_heat_output():
    assert set(protected_outputs(make_home(with_cool=False))) == {"relay_heat"}


def test_only_a_thermostat_protects_its_outputs():
    # The proxy of the second room has an 'output_heat' role: not a regulation.
    assert "relay_ac" not in protected_outputs(make_home())


def test_list_ios_hides_the_protected_outputs():
    listed = ids(run(io.list_ios(FakeClient(make_home()))))
    assert "relay_heat" not in listed and "relay_cool" not in listed
    assert {"light1", "salon_thermo", "relay_other", "relay_ac", "ac"} <= set(listed)


def test_list_ios_still_filters_after_masking():
    listed = run(io.list_ios(FakeClient(make_home()), room="salon",
                             gui_type="light"))
    assert ids(listed) == ["light1", "relay_other"]


def test_find_io_does_not_offer_a_protected_output():
    assert run(io.find_io(FakeClient(make_home()), "relay_heat")) == []
    assert ids(run(io.find_io(FakeClient(make_home()), "relay_other"))) == ["relay_other"]


def test_get_room_does_not_list_a_protected_output():
    room = run(rooms.get_room(FakeClient(make_home()), "Salon"))
    assert "relay_heat" not in ids(room["ios"])
    assert "relay_other" in ids(room["ios"])


# --- refusal ---------------------------------------------------------------

@pytest.mark.parametrize("relay,role", [("relay_heat", "output_heat"),
                                        ("relay_cool", "output_cool")])
def test_a_protected_output_is_refused_in_write(relay, role):
    client = FakeClient(make_home())
    with pytest.raises(ValueError) as err:
        run(io.set_io_state(client, relay, "true"))
    text = str(err.value)
    assert relay in text and "salon_thermo" in text and role in text
    assert "Command the thermostat instead" in text
    assert client.sent == []


def test_the_refusal_does_not_depend_on_a_cached_topology():
    # The cached home predates the thermostat: the write reads a fresh one.
    stale = {"home": [{"name": "Salon", "items": [item("relay_heat")]}]}
    client = FakeClient(stale, fresh=make_home())
    with pytest.raises(ValueError, match="thermostat"):
        run(io.set_io_state(client, "relay_heat", "true"))
    assert client.forced == [True]
    assert client.sent == []


def test_an_ordinary_output_is_still_written():
    client = FakeClient(make_home())
    out = run(io.set_io_state(client, "relay_other", "on"))
    assert client.sent == [("relay_other", "true")]
    assert out["value_sent"] == "true"


def test_the_outputs_of_other_devices_are_still_written():
    client = FakeClient(make_home())
    run(io.set_io_state(client, "relay_ac", "true"))
    assert client.sent == [("relay_ac", "true")]


# --- device command --------------------------------------------------------

def test_the_device_is_listed_as_writable_with_its_object():
    entry = next(e for e in run(io.list_ios(FakeClient(make_home())))
                 if e["id"] == "salon_thermo")
    assert entry["rw"] is True
    assert entry["device"]["capabilities"]["modes"] == ["off", "heat", "cool", "auto"]
    assert entry["device"]["state"]["target_temp"] == 21.0
    assert entry["device"]["ios"]["output_heat"] == "relay_heat"


def test_a_plain_io_has_no_device_key():
    entry = next(e for e in run(io.list_ios(FakeClient(make_home())))
                 if e["id"] == "light1")
    assert "device" not in entry


def test_get_io_state_and_get_room_give_the_device_object():
    state = run(io.get_io_state(FakeClient(make_home()), "salon_thermo"))
    assert state["device"]["state"]["mode"] == "heat"
    room = run(rooms.get_room(FakeClient(make_home()), "Salon"))
    thermo = next(e for e in room["ios"] if e["id"] == "salon_thermo")
    assert thermo["device"]["capabilities"]["target_temp"]["max"] == 35
    plain = run(io.get_io_state(FakeClient(make_home()), "light1"))
    assert "device" not in plain


def test_the_device_object_is_sanitised():
    home = make_home()
    home["home"][0]["items"][1]["device"]["state"]["action"] = "heat\x1b[31m\x07ing"
    entry = next(e for e in run(io.list_ios(FakeClient(home)))
                 if e["id"] == "salon_thermo")
    assert entry["device"]["state"]["action"] == "heating"
    assert entry["device"]["state"]["current_temp"] == 19.5


def test_safe_tree_keeps_numbers_and_bounds_the_depth():
    deep = {"a": {"b": {"c": {"d": {"e": {"f": {"g": 1}}}}}}}
    out = safety.safe_tree(deep)
    assert out["a"]["b"]["c"]["d"]["e"]["f"] is None
    assert safety.safe_tree({"n": 1.5, "t": True, "x": None, "l": ["a\x00"]}) == \
        {"n": 1.5, "t": True, "x": None, "l": ["a"]}


def test_a_json_command_is_sent_to_the_device_compact():
    client = FakeClient(make_home())
    out = run(io.set_io_state(client, "salon_thermo",
                              ' { "mode": "heat", "target_temp": 21.5 } '))
    assert client.sent == [("salon_thermo", '{"mode":"heat","target_temp":21.5}')]
    assert out["value_sent"] == '{"mode":"heat","target_temp":21.5}'


def test_a_plain_mode_is_sent_to_the_device():
    client = FakeClient(make_home())
    run(io.set_io_state(client, "salon_thermo", "off"))
    assert client.sent == [("salon_thermo", "off")]


@pytest.mark.parametrize("command,message", [
    ('{"mode":"heat"', "not valid JSON"),
    ('{}', "non-empty JSON object"),
    ('{"mode":"dry"}', "not available for 'mode'"),
    ('cool', "not offered"),
    ('{"fan_mode":"low"}', "no fan_modes capability"),
    ('{"target_temp_low":18}', "no target_temp_range capability"),
    ('{"target_temp":NaN}', "Invalid number NaN"),
    ('{"target_temp":true}', "string or a number"),
    ('{"target_temp":[21]}', "string or a number"),
])
def test_a_bad_device_command_is_refused_with_the_reason(command, message):
    client = FakeClient(make_home(with_cool=False))
    with pytest.raises(ValueError, match=message):
        run(io.set_io_state(client, "salon_thermo", command))
    assert client.sent == []


def test_a_field_unknown_to_iodoc_is_refused(iodoc_file):
    iodoc_file({"thermostat": DEVICE_DOC})
    client = FakeClient(make_home())
    with pytest.raises(ValueError, match="Unknown field 'targt_temp'"):
        run(io.set_io_state(client, "salon_thermo", '{"targt_temp":21}'))
    run(io.set_io_state(client, "salon_thermo", '{"target_temp":21}'))
    assert client.sent == [("salon_thermo", '{"target_temp":21}')]


def test_without_iodoc_the_fields_are_checked_against_the_capabilities(monkeypatch):
    monkeypatch.delenv(iodoc.ENV_PATH, raising=False)
    client = FakeClient(make_home())
    run(io.set_io_state(client, "salon_thermo", '{"anything":1}'))
    assert client.sent == [("salon_thermo", '{"anything":1}')]


# --- description generated from IODoc --------------------------------------

def test_the_description_is_generated_from_the_iodoc_fields():
    text = iodoc.command_help({"thermostat": DEVICE_DOC})
    assert "Implementation 'thermostat' (climate): A thermostat." in text
    assert "mode: one of off, heat, cool, auto." in text
    assert "target_temp: number from 7 to 35 by 0.5 °C." in text
    assert "The value cool also needs the link output_cool." in text
    assert "Needs the link target_temp." in text
    assert "one of the values of the 'fan_modes' param" in text
    assert "'{\"mode\":\"heat\",\"target_temp\":21}'" in text


def test_the_description_follows_the_document():
    changed = copy.deepcopy(DEVICE_DOC)
    changed["command_fields"][1]["max"] = 30.0
    changed["command_fields"][0]["values"] = ["off", "heat"]
    changed["command_fields"][0]["value_requires_links"] = {}
    text = iodoc.command_help({"thermostat": changed})
    assert "number from 7 to 30" in text
    assert "mode: one of off, heat." in text
    assert "output_cool" not in text


def test_the_example_is_a_command_the_tool_accepts():
    example = iodoc.example({"thermostat": DEVICE_DOC})
    client = FakeClient(make_home())
    run(io.set_io_state(client, "salon_thermo", example))
    assert client.sent == [("salon_thermo", example)]


def test_the_description_says_so_when_iodoc_is_missing():
    from calaos_mcp import server

    text = server.set_io_state_description({})
    assert "io_doc.json unavailable" in text
    assert "output_heat and output_cool" in text
    assert "Implementation" not in text


def test_iodoc_is_read_once_per_version_of_the_file(iodoc_file):
    path = iodoc_file({"thermostat": DEVICE_DOC})
    assert set(iodoc.load_devices()) == {"thermostat"}
    path.write_text(json.dumps({"devices": {"proxy": DEVICE_DOC}}))
    os.utime(path, ns=(1, 2_000_000_000))
    assert set(iodoc.load_devices()) == {"proxy"}


@pytest.mark.parametrize("content", ["not json", "[]", "{}", '{"devices": {}}'])
def test_a_broken_iodoc_leaves_no_documentation(tmp_path, content):
    p = tmp_path / "io_doc.json"
    p.write_text(content)
    assert iodoc.load_devices(str(p)) == {}


def test_a_missing_iodoc_leaves_no_documentation(tmp_path, monkeypatch):
    assert iodoc.load_devices(str(tmp_path / "absent.json")) == {}
    monkeypatch.delenv(iodoc.ENV_PATH, raising=False)
    assert iodoc.load_devices() == {}


def test_a_device_without_json_format_is_not_documented(iodoc_file):
    iodoc_file({"legacy": {"command_fields": []}, "thermostat": DEVICE_DOC})
    assert set(iodoc.load_devices()) == {"thermostat"}


def test_the_real_iodoc_is_rendered_and_its_example_accepted(monkeypatch):
    # make check points CALAOS_IODOC_PATH at what calaos_server --gendoc wrote.
    path = os.environ.get(iodoc.ENV_PATH) or os.path.join(
        os.path.dirname(__file__), "..", "..", "src", "bin", "calaos_mcp",
        "io_doc.json")
    assert os.path.exists(path), "build first: make writes src/bin/calaos_mcp/io_doc.json"
    monkeypatch.setenv(iodoc.ENV_PATH, path)
    devices = iodoc.load_devices()
    assert {"proxy", "thermostat"} <= set(devices)
    text = iodoc.command_help(devices)
    for impl, doc in devices.items():
        assert f"Implementation '{impl}'" in text
        for field in doc["command_fields"]:
            assert f"- {field['name']}:" in text
            assert field["description"] in text
            for value in field.get("values", []):
                assert value in text
    client = FakeClient(make_home())
    run(io.set_io_state(client, "salon_thermo", iodoc.example(devices)))
    assert client.sent


# --- the cached home follows the events ------------------------------------

def event(kind, data):
    return {"type_str": kind, "data": data}


def cached_client():
    c = CalaosClient()
    c._home_cache = make_home()
    return c


def test_an_io_changed_replaces_the_device_object_and_the_state():
    c = cached_client()
    new_device = copy.deepcopy(c._home_cache["home"][0]["items"][1]["device"])
    new_device["ios"]["output_heat"] = "relay_other"
    c._on_event(event("io_changed", {"id": "salon_thermo", "state": "off",
                                     "device": new_device}))
    assert protected_outputs(c._home_cache)["relay_other"] == (
        "salon_thermo", "output_heat")
    assert "relay_heat" not in protected_outputs(c._home_cache)
    assert c._home_cache["home"][0]["items"][1]["state"] == "off"


def test_an_io_changed_updates_the_state_of_a_plain_io():
    c = cached_client()
    c._on_event(event("io_changed", {"id": "light1", "state": "true"}))
    assert c._home_cache["home"][0]["items"][0]["state"] == "true"
    assert "device" not in c._home_cache["home"][0]["items"][0]


def test_an_io_changed_for_an_unknown_io_changes_nothing():
    c = cached_client()
    before = copy.deepcopy(c._home_cache)
    c._on_event(event("io_changed", {"id": "nope", "state": "x"}))
    assert c._home_cache == before


@pytest.mark.parametrize("kind", ["io_added", "io_deleted", "room_added",
                                  "room_deleted"])
def test_a_topology_event_drops_the_cached_home(kind):
    c = cached_client()
    c._on_event(event(kind, {"id": "x"}))
    assert c._home_cache is None


def test_other_events_keep_the_cached_home():
    c = cached_client()
    c._on_event(event("room_changed", {"room_name": "x"}))
    assert c._home_cache is not None
    c._home_cache = None
    c._on_event(event("io_changed", {"id": "light1", "state": "true"}))
    assert c._home_cache is None
