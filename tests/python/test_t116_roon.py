# T1.16 — regression tests for ExternProcRoon_main.py robustness:
# set_volume with an unknown zone must not raise (it would kill the
# extern-proc run loop), state callback must tolerate zones without
# now_playing, and no stray print() debug output.
#
# Run with: python3 -m unittest discover -s tests/python -p 'test_t116_*.py'
# The calaos_extern_proc and roonapi modules are stubbed.

import importlib.util
import io as io_mod
import json
import os
import sys
import types
import unittest
from contextlib import redirect_stdout

_ROON_MAIN = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "bin", "calaos_server", "Audio", "ExternProcRoon_main.py",
))


def _install_stubs():
    if "calaos_extern_proc" not in sys.modules:
        m = types.ModuleType("calaos_extern_proc")

        def _log_factory(dom):
            return lambda *a, **k: None

        m.cDebugDom = _log_factory
        m.cInfoDom = _log_factory
        m.cErrorDom = _log_factory
        m.cCriticalDom = _log_factory
        m.cWarningDom = _log_factory
        m.configure_logger = lambda *a, **k: None

        class ExternProcClient:
            def __init__(self):
                self.sockpath = ""
                self.name = ""
                self.cachePath = "/tmp"

            def stop(self):
                return True

            def run(self, timeout_ms):
                return True

            def connect_socket(self):
                return True

            def send_message(self, msg):
                pass

        m.ExternProcClient = ExternProcClient
        sys.modules["calaos_extern_proc"] = m

    if "roonapi" not in sys.modules:
        r = types.ModuleType("roonapi")
        r.RoonApi = object
        r.RoonDiscovery = object
        sys.modules["roonapi"] = r


def _load_roon_module():
    _install_stubs()
    spec = importlib.util.spec_from_file_location("extern_proc_roon_main", _ROON_MAIN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FakeRoonApi:
    def __init__(self, zones):
        self.zones = zones
        self.volume_calls = []
        self.playback_calls = []

    def set_volume_percent(self, output_id, volume):
        self.volume_calls.append((output_id, volume))

    def playback_control(self, zid, action):
        self.playback_calls.append((zid, action))

    def get_image(self, image_id):
        return f"http://core/image/{image_id}"


class RoonRobustnessTest(unittest.TestCase):
    def setUp(self):
        self.mod = _load_roon_module()
        self.client = self.mod.RoonClient()
        self.zones = {
            "z1": {
                "display_name": "Living room",
                "outputs": [{"output_id": "out1"}],
            },
            "z_no_out": {"display_name": "Bare zone"},
        }
        self.client.roon_api = FakeRoonApi(self.zones)

    def _msg(self, **kw):
        self.client.message_received(json.dumps(kw))

    def test_set_volume_unknown_zone_does_not_raise(self):
        # Regression: used to KeyError on zones[zid] before the guard,
        # crashing the extern-proc run loop.
        self._msg(action="set_volume", zone_id="ghost", volume=50)
        self.assertEqual(self.client.roon_api.volume_calls, [])

    def test_set_volume_missing_zone_id_does_not_raise(self):
        # Regression: zones[None] used to raise TypeError/KeyError.
        self._msg(action="set_volume", volume=50)
        self.assertEqual(self.client.roon_api.volume_calls, [])

    def test_set_volume_zone_without_outputs_does_not_raise(self):
        self._msg(action="set_volume", zone_id="z_no_out", volume=50)
        self.assertEqual(self.client.roon_api.volume_calls, [])

    def test_set_volume_valid_zone(self):
        self._msg(action="set_volume", zone_id="z1", volume=42)
        self.assertEqual(self.client.roon_api.volume_calls, [("out1", 42)])

    def test_state_callback_zone_without_now_playing(self):
        # Regression: zone["now_playing"] used to KeyError on stopped zones.
        # Also asserts the leftover print() debug output is gone.
        self.client.subscribed_zones = ["z_no_out"]
        buf = io_mod.StringIO()
        with redirect_stdout(buf):
            self.client.roon_state_received("zones_changed", ["z_no_out", "gone_zone"])
        self.assertEqual(buf.getvalue(), "")

    def test_parse_cover_url_without_now_playing(self):
        zone = {"display_name": "Idle"}
        out = self.client.parse_cover_url(zone)
        self.assertNotIn("cover_url", out)

    def test_parse_cover_url_with_image(self):
        zone = {"now_playing": {"image_key": "img42"}}
        out = self.client.parse_cover_url(zone)
        self.assertEqual(out["cover_url"], "http://core/image/img42")

    def test_playback_unknown_zone_does_not_raise(self):
        self._msg(action="play", zone_id="ghost")
        self.assertEqual(self.client.roon_api.playback_calls, [])


if __name__ == "__main__":
    unittest.main()
