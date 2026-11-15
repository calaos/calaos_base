# Tests of the pure parts of IO/Klf200/ExternProcKlf200_main.py: the command
# grammar it accepts from calaos_server, the node state frame it sends back,
# the rain reading, and the dispatch of a decoded message. No gateway, no
# asyncio loop: calaos_extern_proc and pyvlx are stubbed.
#
# Run with: python3 -m unittest discover -s tests/python -p 'test_klf200.py'

import asyncio
import importlib.util
import json
import os
import sys
import types
import unittest

_VELUX_MAIN = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "bin", "calaos_server", "IO", "Klf200", "ExternProcKlf200_main.py",
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
                self.sent = []

            def stop(self):
                return True

            def run(self, timeout_ms):
                return True

            def connect_socket(self):
                return True

            def send_message(self, msg):
                self.sent.append(json.loads(msg))

        m.ExternProcClient = ExternProcClient
        sys.modules["calaos_extern_proc"] = m

    if "pyvlx" not in sys.modules:
        v = types.ModuleType("pyvlx")

        class Node:
            pass

        class OpeningDevice(Node):
            pass

        class Window(OpeningDevice):
            pass

        class Position:
            def __init__(self, position_percent=0):
                self.position_percent = position_percent
                self.known = True

        v.Node = Node
        v.OpeningDevice = OpeningDevice
        v.Window = Window
        v.Position = Position
        v.PyVLX = object
        v.PyVLXException = Exception
        sys.modules["pyvlx"] = v


def _load_klf200_module():
    _install_stubs()
    spec = importlib.util.spec_from_file_location("extern_proc_klf200_main", _VELUX_MAIN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FakePosition:
    def __init__(self, percent, known=True):
        self.position_percent = percent
        self.known = known


class FakeNode:
    def __init__(self, node_id, name="Bureau", percent=42, known=True,
                 opening=False, closing=False, rain_sensor=None):
        self.node_id = node_id
        self.name = name
        self.position = FakePosition(percent, known)
        self.is_opening = opening
        self.is_closing = closing
        if rain_sensor is not None:
            self.rain_sensor = rain_sensor

    def get_position(self):
        return self.position


class ParseCommandTest(unittest.TestCase):
    def setUp(self):
        self.mod = _load_klf200_module()

    def test_register_is_normalized(self):
        cmd, err = self.mod.parse_command(
            {"action": "register", "host": "klf", "password": "pw", "node_id": 3, "rain": True})
        self.assertIsNone(err)
        self.assertEqual(cmd, {"action": "register", "host": "klf", "password": "pw",
                               "node_id": 3, "rain": True})

    def test_register_defaults_rain_to_false(self):
        cmd, _ = self.mod.parse_command(
            {"action": "register", "host": "klf", "password": "pw", "node_id": 3})
        self.assertFalse(cmd["rain"])

    def test_register_without_password_is_dropped(self):
        cmd, err = self.mod.parse_command({"action": "register", "host": "klf", "node_id": 3})
        self.assertIsNone(cmd)
        self.assertIn("password", err)

    def test_set_position_is_clamped(self):
        cmd, _ = self.mod.parse_command(
            {"action": "set_position", "host": "klf", "node_id": 3, "position": 250})
        self.assertEqual(cmd["position"], 100)
        cmd, _ = self.mod.parse_command(
            {"action": "set_position", "host": "klf", "node_id": 3, "position": -7})
        self.assertEqual(cmd["position"], 0)

    def test_set_position_without_a_number_is_dropped(self):
        cmd, err = self.mod.parse_command(
            {"action": "set_position", "host": "klf", "node_id": 3, "position": "half"})
        self.assertIsNone(cmd)
        self.assertIn("position", err)

    def test_node_id_accepts_the_string_spelling_and_refuses_garbage(self):
        # calaos_server sends a JSON number, but a string of digits is fine too
        cmd, _ = self.mod.parse_command({"action": "stop", "host": "klf", "node_id": "5"})
        self.assertEqual(cmd["node_id"], 5)
        for bad in (None, "x", -1, "2.5"):
            cmd, err = self.mod.parse_command({"action": "stop", "host": "klf", "node_id": bad})
            self.assertIsNone(cmd, bad)
            self.assertIn("node_id", err)

    def test_unknown_action_and_non_object_are_dropped(self):
        self.assertIsNone(self.mod.parse_command({"action": "reboot", "host": "h", "node_id": 1})[0])
        self.assertIsNone(self.mod.parse_command(["register"])[0])
        self.assertIsNone(self.mod.parse_command({"action": "stop", "node_id": 1})[0])

    def test_redact_hides_the_password_only(self):
        r = self.mod.redact({"action": "register", "host": "h", "password": "secret", "node_id": 1})
        self.assertEqual(r["password"], "<redacted>")
        self.assertEqual(r["host"], "h")
        self.assertNotIn("secret", json.dumps(r))


class NodeStateTest(unittest.TestCase):
    def setUp(self):
        self.mod = _load_klf200_module()

    def test_state_frame_shape(self):
        node = FakeNode(3, percent=42, opening=True, rain_sensor=True)
        msg = self.mod.node_state_message("klf", node)
        self.assertEqual(msg, {
            "event": "node", "host": "klf", "node_id": 3, "position": 42,
            "moving": "up", "node_type": "FakeNode", "name": "Bureau", "rain_sensor": True,
        })

    def test_moving_states(self):
        self.assertEqual(self.mod.moving_state(FakeNode(1, opening=True)), "up")
        self.assertEqual(self.mod.moving_state(FakeNode(1, closing=True)), "down")
        self.assertEqual(self.mod.moving_state(FakeNode(1)), "stop")

    def test_unknown_position_is_minus_one(self):
        self.assertEqual(self.mod.node_position_percent(FakeNode(1, known=False)), -1)

    def test_node_without_rain_sensor_attribute_reports_false(self):
        msg = self.mod.node_state_message("klf", FakeNode(1))
        self.assertFalse(msg["rain_sensor"])

    def test_rain_is_a_limitation_of_89_percent_or_more(self):
        # Values seen by the Home Assistant integration across models.
        for wet in (89, 91, 93, 100):
            self.assertTrue(self.mod.rain_from_limitation(wet), wet)
        for dry in (0, 50, 88):
            self.assertFalse(self.mod.rain_from_limitation(dry), dry)


class MessageReceivedTest(unittest.TestCase):
    def setUp(self):
        self.mod = _load_klf200_module()
        self.client = self.mod.Klf200Client()
        # Under pytest the real calaos_extern_proc is importable (conftest),
        # so record what the client sends here rather than in the stub.
        self.client.sent = []
        self.client.send_message = lambda msg: self.client.sent.append(json.loads(msg))

    def test_garbage_and_invalid_messages_do_not_raise(self):
        # Before the loop exists, anything must be dropped quietly: an
        # exception here would kill the extern-proc run loop.
        self.client.message_received("{not json")
        self.client.message_received(json.dumps({"action": "fly"}))
        self.client.message_received(json.dumps({"action": "stop", "host": "h", "node_id": 1}))
        self.assertEqual(self.client.sent, [])

    def test_send_error_frame_shape(self):
        self.client.send_error("klf", self.mod.ERR_NODE_NOT_FOUND, "no such node", 4)
        self.assertEqual(self.client.sent, [{
            "status": "error", "host": "klf", "code": "node_not_found",
            "message": "no such node", "node_id": 4,
        }])
        self.client.sent.clear()
        self.client.send_error("klf", self.mod.ERR_GATEWAY_CONNECT_FAILED, "timeout")
        self.assertNotIn("node_id", self.client.sent[0])

    def test_error_codes_match_the_cpp_vocabulary(self):
        # Mirror of Klf200Wire::isKnownErrorCode(); tests/Klf200Wire_test.cpp
        # pins the same five strings on the other end.
        self.assertEqual(
            {self.mod.ERR_GATEWAY_CONNECT_FAILED, self.mod.ERR_GATEWAY_NOT_CONNECTED,
             self.mod.ERR_NODE_NOT_FOUND, self.mod.ERR_NODE_UNSUPPORTED, self.mod.ERR_COMMAND_FAILED},
            {"gateway_connect_failed", "gateway_not_connected", "node_not_found",
             "node_unsupported", "command_failed"})


class FakeConnection:
    """What Gateway uses of pyvlx.Connection: a transport that a connect()
    opens and a disconnect() closes, and the two callback lists."""

    def __init__(self):
        self.connected = False
        self.open_transports = 0
        self.closed_cbs = []
        self.opened_cbs = []

    def register_connection_closed_cb(self, cb):
        self.closed_cbs.append(cb)

    def register_connection_opened_cb(self, cb):
        self.opened_cbs.append(cb)

    def disconnect(self, notify_callbacks=True):
        if self.connected:
            self.open_transports -= 1
        self.connected = False


class FakePyVLX:
    """pyvlx.PyVLX as Gateway drives it. connect() opens the transport first
    and then fails when fail_auth is set, like the real one when
    password_enter() raises: the socket stays open."""

    def __init__(self, fail_auth=False):
        self.connection = FakeConnection()
        self.fail_auth = fail_auth
        self.connect_calls = 0
        self.nodes = {}

    def get_connected(self):
        return self.connection.connected

    async def connect(self):
        self.connect_calls += 1
        self.connection.connected = True
        self.connection.open_transports += 1
        for cb in self.connection.opened_cbs:
            await cb()
        if self.fail_auth:
            raise RuntimeError("password rejected")

    async def load_nodes(self):
        pass


class GatewayTest(unittest.TestCase):
    """The session handling of Gateway against a fake pyvlx, on a real loop."""

    def setUp(self):
        self.mod = _load_klf200_module()
        self.client = self.mod.Klf200Client()
        self.client.sent = []
        self.client.send_message = lambda msg: self.client.sent.append(json.loads(msg))
        self.mod.RECONNECT_DELAY_FIRST = 0.01
        self.mod.RECONNECT_DELAY_MAX = 0.01

    def _gateway(self, vlx):
        gw = self.mod.Gateway(self.client, "klf", "pw", 300)
        gw.pyvlx = vlx
        vlx.connection.register_connection_closed_cb(gw._on_connection_closed)
        vlx.connection.register_connection_opened_cb(gw._on_connection_opened)
        return gw

    def test_a_failed_attempt_closes_the_session_it_opened(self):
        vlx = FakePyVLX(fail_auth=True)
        gw = self._gateway(vlx)

        async def scenario():
            task = asyncio.create_task(gw._run())
            await asyncio.sleep(0.1)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        asyncio.run(scenario())
        self.assertGreater(vlx.connect_calls, 2)  # it did retry
        # Every retry closed what the previous one opened: at most the
        # attempt in flight holds a socket, never one per retry.
        self.assertLessEqual(vlx.connection.open_transports, 1)
        self.assertFalse(gw.connected)

    def test_a_session_reopened_by_pyvlx_is_not_connected_twice(self):
        vlx = FakePyVLX()
        vlx.connection.connected = True  # the heartbeat reconnected
        vlx.connection.open_transports = 1
        gw = self._gateway(vlx)

        asyncio.run(gw._connect())
        self.assertEqual(vlx.connect_calls, 0)
        self.assertEqual(vlx.connection.open_transports, 1)
        self.assertTrue(gw.connected)
        self.assertIn({"status": "gateway", "host": "klf", "connected": True}, self.client.sent)

    def test_the_retry_wait_ends_when_pyvlx_reopens_the_session(self):
        gw = self._gateway(FakePyVLX())

        async def scenario():
            loop = asyncio.get_running_loop()
            start = loop.time()
            loop.call_later(0.05, gw._opened.set)  # what the opened callback does
            await gw._wait_retry(30)
            return loop.time() - start

        self.assertLess(asyncio.run(scenario()), 5)

    def test_a_rain_registration_gets_the_current_reading_even_if_unchanged(self):
        mod = self.mod

        class WetWindow(mod.Window):
            node_id = 4
            name = "Fenetre"
            rain_sensor = True
            is_opening = False
            is_closing = False
            device_updated_cbs = []
            position = FakePosition(100)

            def get_position(self):
                return self.position

            def register_device_updated_cb(self, cb):
                self.device_updated_cbs.append(cb)

            def is_moving(self):
                return False

            async def get_limitation_min(self):
                return FakePosition(93)

        vlx = FakePyVLX()
        vlx.nodes = {4: WetWindow()}
        gw = self._gateway(vlx)

        def rain_frames():
            return [m for m in self.client.sent if m.get("event") == "rain"]

        async def scenario():
            await gw._connect()
            await gw.register(4, rain=True)
            first = len(rain_frames())
            # Same node, new rain IO (config reload): the state did not
            # change, it must still be told.
            await gw.register(4, rain=True)
            return first, len(rain_frames())

        first, second = asyncio.run(scenario())
        self.assertEqual(first, 1)
        self.assertEqual(second, 2)
        self.assertTrue(rain_frames()[-1]["rain"])


if __name__ == "__main__":
    unittest.main()
