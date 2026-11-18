# set_io_state and the IOs of a climate program: the guard follows the owned links
# published in device.ios, never the shape of an id, and leaves every other IO alone.
import pytest

from calaos_mcp.tools import io
from calaos_mcp.tools._home import program_ios

from test_mcp_devices import FakeClient, item, run

LEVEL_ROLES = [f"{mode}_level{n}{tail}"
               for mode in ("heat", "cool") for n in range(1, 7)
               for tail in ("", "_setpoint")]
OTHER_ROLES = ["heat_default", "cool_default", "cool_default_off", "program_enabled"]


def message(io_id, role, device="chauffage"):
    return (f"IO '{io_id}' is the {role} of the program of the climate '{device}' and "
            "cannot be written through MCP. Ask the user to change the program in the "
            "Calaos app.")


def climate(device_id, links, **kw):
    return item(device_id, gui_type="climate", var_type="string", io_type="inout",
                state="heat", device={"family": "climate", "impl": "proxy",
                                      "capabilities": {"modes": ["off", "heat"]},
                                      "ios": links}, **kw)


def plain(io_id, **kw):
    kw = {"io_type": "inout", "var_type": "float", "gui_type": "var_int",
          "state": "20", "visible": "false", **kw}
    return item(io_id, **kw)


def home_of(*items):
    return {"home": [{"name": "Salon", "items": list(items)}]}


def full_climate(device_id, suffix=""):
    links = {"current_temp": f"{device_id}_temp", "target_temp": f"{device_id}_target"}
    links.update({r: f"{device_id}_{r}{suffix}" for r in LEVEL_ROLES + OTHER_ROLES})
    owned = [plain(t, io_type="input" if r.startswith(("heat_level", "cool_level"))
                   and not r.endswith("_setpoint") else "inout")
             for r, t in links.items() if r in LEVEL_ROLES + OTHER_ROLES]
    return [climate(device_id, links), plain(links["current_temp"]),
            plain(links["target_temp"]), *owned], links


@pytest.mark.parametrize("role", LEVEL_ROLES + OTHER_ROLES)
def test_every_role_of_the_program_is_refused_even_for_a_read_only_io(role):
    # The ranges are inputs on the real server: the program message must win over
    # the generic "read-only" one.
    items, links = full_climate("chauffage")
    client = FakeClient(home_of(*items))
    with pytest.raises(ValueError) as err:
        run(io.set_io_state(client, links[role], "1"))
    assert str(err.value) == message(links[role], role)
    assert client.sent == []


def test_program_ios_maps_each_owned_id_to_its_climate_and_role():
    items, links = full_climate("chauffage")
    got = program_ios(home_of(*items))
    assert got == {links[r]: ("chauffage", r) for r in LEVEL_ROLES + OTHER_ROLES}


def test_two_climates_are_both_guarded_whatever_their_suffixes():
    a, links_a = full_climate("salon", "_2")
    b, links_b = full_climate("chambre", "_7")
    client = FakeClient(home_of(*a, *b))
    for device, links in (("salon", links_a), ("chambre", links_b)):
        for role in ("program_enabled", "cool_level2_setpoint"):
            with pytest.raises(ValueError) as err:
                run(io.set_io_state(client, links[role], "1"))
            assert str(err.value) == message(links[role], role, device)
    assert client.sent == []


def test_an_ordinary_io_named_like_a_program_io_is_still_written():
    # Detection by name would refuse these: nothing owns them.
    items, _ = full_climate("chauffage")
    extra = [plain("chauffage_program_enabled_9", io_type="output"),
             plain("garage_heat_default"), plain("heat_level1_setpoint")]
    client = FakeClient(home_of(*items, *extra))
    for e in extra:
        run(io.set_io_state(client, e["id"], "21"))
    assert [s[0] for s in client.sent] == [e["id"] for e in extra]


def test_the_climate_and_its_non_program_links_are_still_written():
    items, links = full_climate("chauffage")
    client = FakeClient(home_of(*items))
    run(io.set_io_state(client, "chauffage", "off"))
    run(io.set_io_state(client, links["current_temp"], "19"))
    run(io.set_io_state(client, links["target_temp"], "21"))
    assert [s[0] for s in client.sent] == [
        "chauffage", links["current_temp"], links["target_temp"]]


@pytest.mark.parametrize("role", [
    "heat_level", "level1", "heat_level1_setpoints", "heat_level1_set", "xheat_default",
    "heat_default_2", "cool_default_offf", "program_enabled_x", "pre_program_enabled",
    "Heat_Default", "output_heat", "mode", ""])
def test_a_link_role_that_only_resembles_a_program_role_is_not_refused(role):
    links = {"current_temp": "t", role: "owned_io"}
    client = FakeClient(home_of(climate("chauffage", links), plain("t"), plain("owned_io")))
    run(io.set_io_state(client, "owned_io", "1"))
    assert client.sent == [("owned_io", "1")]
    assert program_ios(client.home) == {}


def test_a_program_added_after_the_cached_topology_is_still_refused():
    stale = home_of(plain("heat_target"))
    items, links = full_climate("chauffage")
    client = FakeClient(stale, fresh=home_of(*items))
    with pytest.raises(ValueError, match="program_enabled of the program"):
        run(io.set_io_state(client, links["program_enabled"], "false"))
    assert client.forced == [True]
    assert client.sent == []


@pytest.mark.parametrize("ios", [None, [], "x", 3, {"program_enabled": None},
                                 {"program_enabled": ""}, {"program_enabled": 5},
                                 {"program_enabled": ["a"]}])
def test_a_malformed_device_does_not_hide_the_program_of_another(ios):
    items, links = full_climate("chauffage")
    broken = item("broken", gui_type="climate", io_type="inout", var_type="string",
                  device={"family": "climate", "impl": "proxy", "ios": ios})
    odd = item("odd", device="not an object")
    client = FakeClient(home_of(broken, odd, *items))
    with pytest.raises(ValueError, match="heat_default of the program"):
        run(io.set_io_state(client, links["heat_default"], "17"))
    run(io.set_io_state(client, links["target_temp"], "21"))
    assert client.sent == [(links["target_temp"], "21")]
