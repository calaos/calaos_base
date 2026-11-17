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

    def test_rain_is_read_strictly(self):
        # bool("false") would be True
        for value, expected in ((True, True), ("true", True), (False, False),
                                ("false", False), ("yes", False), (1, False)):
            cmd, _ = self.mod.parse_command(
                {"action": "register", "host": "klf", "password": "pw", "node_id": 3, "rain": value})
            self.assertIs(cmd["rain"], expected, value)

    def test_unregister_needs_host_and_node_only(self):
        cmd, err = self.mod.parse_command({"action": "unregister", "host": "klf", "node_id": 3})
        self.assertIsNone(err)
        self.assertEqual(cmd, {"action": "unregister", "host": "klf", "node_id": 3})

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


class FakeHeartbeat:
    def __init__(self):
        self.running = False
        self.stop_calls = 0

    async def stop(self):
        self.stop_calls += 1
        self.running = False


class FakeKlf200:
    """pyvlx.Klf200Gateway: get_state() raises when the gateway does not
    answer, like the real one."""

    def __init__(self):
        self.answers = True
        self.get_state_calls = 0
        self.house_status_monitor_enabled = True
        self.monitor_disabled = False
        self.rebooted = False

    async def get_state(self):
        self.get_state_calls += 1
        if not self.answers:
            raise RuntimeError("Unable to retrieve state")
        return True

    async def house_status_monitor_disable(self, pyvlx, timeout=10):
        self.house_status_monitor_enabled = False
        self.monitor_disabled = True

    async def reboot(self):
        self.rebooted = True


class FakePyVLX:
    """pyvlx.PyVLX as Gateway drives it. connect() opens the transport first
    and then fails when fail_auth is set, like the real one when
    password_enter() raises: the socket stays open."""

    def __init__(self, fail_auth=False):
        self.connection = FakeConnection()
        self.heartbeat = FakeHeartbeat()
        self.klf200 = FakeKlf200()
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
        self.heartbeat.running = True  # the last step of pyvlx.connect()

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


def _wet_window_class(mod, node_id=4, limitation=93):
    class WetWindow(mod.Window):
        name = "Fenetre"
        rain_sensor = True
        is_opening = False
        is_closing = False
        position = FakePosition(100)

        def __init__(self):
            self.node_id = node_id
            self.device_updated_cbs = []

        def get_position(self):
            return self.position

        def register_device_updated_cb(self, cb):
            self.device_updated_cbs.append(cb)

        def is_moving(self):
            return False

        async def get_limitation_min(self):
            return FakePosition(limitation)

    return WetWindow


class SessionRobustnessTest(unittest.TestCase):
    """B1-B3 of the review of PR 278: silent peer, two reconnection paths,
    clean shutdown."""

    setUp = GatewayTest.setUp
    _gateway = GatewayTest._gateway

    def test_a_closed_session_stops_the_pyvlx_heartbeat(self):
        # Otherwise the heartbeat reconnects through send_frame() while
        # _run() does the same: two sessions, a leaked socket.
        vlx = FakePyVLX()
        gw = self._gateway(vlx)

        async def scenario():
            await gw._connect()
            self.assertTrue(vlx.heartbeat.running)
            vlx.connection.disconnect()
            for cb in vlx.connection.closed_cbs:
                await cb()

        asyncio.run(scenario())
        self.assertFalse(vlx.heartbeat.running)
        self.assertTrue(gw._closed.is_set())

    def test_a_failed_attempt_stops_a_heartbeat_it_started(self):
        vlx = FakePyVLX()
        gw = self._gateway(vlx)

        async def failing_load():
            raise RuntimeError("load_nodes failed")
        vlx.load_nodes = failing_load

        async def scenario():
            task = asyncio.create_task(gw._run())
            await asyncio.sleep(0.05)
            task.cancel()
            try:
                await task
            except asyncio.CancelledError:
                pass

        asyncio.run(scenario())
        self.assertGreater(vlx.heartbeat.stop_calls, 0)
        self.assertLessEqual(vlx.connection.open_transports, 1)

    def test_the_watchdog_drops_a_silent_session(self):
        self.mod.WATCHDOG_INTERVAL = 0.01
        vlx = FakePyVLX()
        gw = self._gateway(vlx)

        async def scenario():
            await gw._connect()
            await asyncio.sleep(0.05)
            calls_while_answering = vlx.klf200.get_state_calls
            self.assertFalse(gw._closed.is_set())
            vlx.klf200.answers = False  # power cut: no FIN, no answer
            for _ in range(100):
                if gw._closed.is_set():
                    break
                await asyncio.sleep(0.01)
            gw._watchdog_task.cancel()
            gw._rain_task.cancel()
            return calls_while_answering

        calls = asyncio.run(scenario())
        self.assertGreater(calls, 0)
        self.assertTrue(gw._closed.is_set(), "a silent gateway must end the session")
        self.assertFalse(vlx.connection.connected)
        self.assertEqual(vlx.connection.open_transports, 0)
        self.assertFalse(vlx.heartbeat.running)

    def test_the_watchdog_never_probes_a_session_that_is_down(self):
        # do_api_call() reconnects first when not connected: that is _run()'s job
        self.mod.WATCHDOG_INTERVAL = 0.01
        vlx = FakePyVLX()
        gw = self._gateway(vlx)

        async def scenario():
            await gw._connect()
            gw._set_connected(False)
            before = vlx.klf200.get_state_calls
            await asyncio.sleep(0.05)
            gw._watchdog_task.cancel()
            gw._rain_task.cancel()
            return before

        before = asyncio.run(scenario())
        self.assertEqual(vlx.klf200.get_state_calls, before)

    def test_shutdown_closes_the_session_without_rebooting_the_gateway(self):
        vlx = FakePyVLX()
        gw = self._gateway(vlx)

        async def scenario():
            await gw._connect()
            await gw.shutdown()

        asyncio.run(scenario())
        self.assertTrue(vlx.klf200.monitor_disabled)
        self.assertFalse(vlx.klf200.rebooted)
        self.assertFalse(vlx.heartbeat.running)
        self.assertEqual(vlx.connection.open_transports, 0)

    def test_rain_is_read_again_after_a_reconnection(self):
        vlx = FakePyVLX()
        vlx.nodes = {4: _wet_window_class(self.mod)()}
        gw = self._gateway(vlx)

        async def scenario():
            await gw.register(4, rain=True)  # before the session: nothing read
            self.assertEqual([m for m in self.client.sent if m.get("event") == "rain"], [])
            await gw._connect()
            gw._watchdog_task.cancel()
            gw._rain_task.cancel()

        asyncio.run(scenario())
        rain = [m for m in self.client.sent if m.get("event") == "rain"]
        self.assertEqual(rain, [{"event": "rain", "host": "klf", "node_id": 4, "rain": True}])


class LifecycleTest(unittest.TestCase):
    def setUp(self):
        self.mod = _load_klf200_module()
        self.client = self.mod.Klf200Client()
        self.client.sent = []
        self.client.send_message = lambda msg: self.client.sent.append(json.loads(msg))

    def test_sigterm_only_ends_the_run_loop(self):
        import signal
        saved = (signal.getsignal(signal.SIGTERM), signal.getsignal(signal.SIGINT))
        stops = []
        self.client.stop = lambda: stops.append(True)
        self.client.close = lambda: self.fail("close() must not run in the handler")
        try:
            self.client.install_signal_handlers()
            os.kill(os.getpid(), signal.SIGTERM)
            self.assertEqual(stops, [True])
        finally:
            signal.signal(signal.SIGTERM, saved[0])
            signal.signal(signal.SIGINT, saved[1])

    def test_close_shuts_every_gateway_down_on_the_loop_thread(self):
        import threading
        loop = asyncio.new_event_loop()
        thread = threading.Thread(target=loop.run_forever, daemon=True)
        thread.start()
        self.client.event_loop = loop

        shut = []

        class Gw:
            async def shutdown(self):
                shut.append(threading.current_thread() is thread)

        self.client.gateways = {"a": Gw(), "b": Gw()}
        self.client.close()
        thread.join(timeout=5)
        self.assertEqual(shut, [True, True])
        self.assertEqual(self.client.gateways, {})
        self.assertFalse(thread.is_alive(), "the loop must be stopped")
        loop.close()
        self.client.close()  # closed loop: nothing to do, no raise

    def test_main_closes_the_client_when_run_returns(self):
        calls = []
        mod = self.mod

        class Client:
            cli_list = False

            def setup(self):
                return True

            def install_signal_handlers(self):
                calls.append("signals")

            def run(self, timeout_ms):
                calls.append("run")
                raise RuntimeError("socket gone")

            def close(self):
                calls.append("close")

        saved = mod.Klf200Client
        mod.Klf200Client = Client
        try:
            with self.assertRaises(RuntimeError):
                mod.main()
        finally:
            mod.Klf200Client = saved
        self.assertEqual(calls, ["signals", "run", "close"])

    def test_list_password_comes_from_the_environment_never_argv(self):
        self.assertEqual(self.mod.read_list_password({"KLF200_PASSWORD": "pw"}), "pw")

        class NoTty:
            def isatty(self):
                return False

        saved = sys.stdin
        sys.stdin = NoTty()
        try:
            self.assertIsNone(self.mod.read_list_password({}))
        finally:
            sys.stdin = saved

        class Tty:
            def isatty(self):
                return True

        sys.stdin = Tty()
        try:
            self.assertEqual(self.mod.read_list_password({}, prompt=lambda _p: "typed"), "typed")
        finally:
            sys.stdin = saved

    def test_a_dispatch_aborted_by_a_base_exception_is_logged(self):
        logged = []
        saved = self.mod.cErrorDom
        self.mod.cErrorDom = lambda dom: (lambda *a: logged.append(a))
        try:
            async def scenario():
                fut = asyncio.get_running_loop().create_future()
                fut.set_exception(SystemExit(3))
                self.mod.log_future_failure(fut, "klf", "stop")
                ok = asyncio.get_running_loop().create_future()
                ok.set_result(None)
                self.mod.log_future_failure(ok, "klf", "stop")
                cancelled = asyncio.get_running_loop().create_future()
                cancelled.cancel()
                self.mod.log_future_failure(cancelled, "klf", "stop")
            asyncio.run(scenario())
        finally:
            self.mod.cErrorDom = saved
        self.assertEqual(len(logged), 1)


class RegistrationLifecycleTest(unittest.TestCase):
    """unregister, and a gateway whose nodes are all gone."""

    def setUp(self):
        self.mod = _load_klf200_module()
        self.mod.IDLE_GATEWAY_CLOSE_DELAY = 0.02
        self.client = self.mod.Klf200Client()
        self.client.sent = []
        self.client.send_message = lambda msg: self.client.sent.append(json.loads(msg))
        self.created = []
        mod = self.mod
        created = self.created
        real_unregister = mod.Gateway.unregister

        class Gw:
            def __init__(self, client, host, password, rain_poll_interval):
                self.host = host
                self.password = password
                self.registrations = {}
                self.idle_task = None
                self.shut = False
                created.append(self)

            def start(self):
                pass

            async def register(self, node_id, rain):
                if self.idle_task is not None:
                    self.idle_task.cancel()
                    self.idle_task = None
                self.registrations[node_id] = {"rain": rain}

            def unregister(self, node_id):
                return real_unregister(self, node_id)

            async def shutdown(self):
                if self.idle_task is not None:
                    self.idle_task.cancel()
                self.shut = True

        Gw.rain_state = {}
        self.saved_gateway = mod.Gateway
        mod.Gateway = Gw
        self.Gw = Gw

    def tearDown(self):
        self.mod.Gateway = self.saved_gateway

    def _register(self, node, password="pw"):
        return self.client.dispatch({"action": "register", "host": "klf", "password": password,
                                     "node_id": node, "rain": False})

    def _unregister(self, node):
        return self.client.dispatch({"action": "unregister", "host": "klf", "node_id": node})

    def test_a_gateway_without_nodes_closes_after_the_grace_delay(self):
        async def scenario():
            await self._register(1)
            await self._unregister(1)
            self.assertIn("klf", self.client.gateways)
            await asyncio.sleep(0.1)

        asyncio.run(scenario())
        self.assertNotIn("klf", self.client.gateways)
        self.assertTrue(self.created[0].shut)

    def test_a_node_registered_again_keeps_the_session(self):
        async def scenario():
            await self._register(1)
            await self._register(2)
            await self._unregister(1)
            await asyncio.sleep(0.05)  # node 2 still there
            await self._unregister(2)
            await self._register(2)    # config reload
            await asyncio.sleep(0.1)

        asyncio.run(scenario())
        self.assertEqual(len(self.created), 1)
        self.assertFalse(self.created[0].shut)
        self.assertIs(self.client.gateways["klf"], self.created[0])

    def test_a_new_password_applies_once_the_old_nodes_are_gone(self):
        async def scenario():
            await self._register(1, "old")
            await self._register(2, "new")   # node 1 still uses the old one
            self.assertEqual(self.client.gateways["klf"].password, "old")
            await self._unregister(1)
            await self._unregister(2)
            await self._register(1, "new")

        asyncio.run(scenario())
        self.assertEqual(len(self.created), 2)
        self.assertTrue(self.created[0].shut)
        self.assertEqual(self.client.gateways["klf"].password, "new")

    def test_unregister_of_an_unknown_gateway_is_dropped(self):
        asyncio.run(self._unregister(1))
        self.assertEqual(self.client.gateways, {})


if __name__ == "__main__":
    unittest.main()
