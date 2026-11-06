# Regression tests for ExternProcReolink_main.py supervision: a camera that is
# unreachable when registered, or whose event subscription dies, must keep
# being retried for as long as the process lives.
#
# Run with: python3 -m unittest discover -s tests/python -p 'test_reolink.py'
# The calaos_extern_proc and reolink_aio modules are stubbed.

import asyncio
import importlib.util
import json
import logging
import os
import sys
import types
import unittest
from unittest import mock

_REOLINK_MAIN = os.path.abspath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "bin", "calaos_server", "IO", "Reolink", "ExternProcReolink_main.py",
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

    if "reolink_aio" not in sys.modules:
        pkg = types.ModuleType("reolink_aio")
        api = types.ModuleType("reolink_aio.api")
        api.Host = object
        pkg.api = api
        sys.modules["reolink_aio"] = pkg
        sys.modules["reolink_aio.api"] = api


def _load_module():
    _install_stubs()
    spec = importlib.util.spec_from_file_location("reolink_main_under_test", _REOLINK_MAIN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FakeBaichuan:
    def __init__(self):
        self.events_active = False
        self.callbacks = {}
        self.unsubscribed = False

    def register_callback(self, callback_id, callback, cmd_id=None, channel=None):
        self.callbacks[(cmd_id, channel)] = callback

    async def subscribe_events(self):
        self.events_active = True

    async def unsubscribe_events(self):
        self.unsubscribed = True
        self.events_active = False

    async def check_subscribe_events(self):
        pass

    def push(self, channel=0):
        self.callbacks[(33, channel)]()


class FakeHost:
    def __init__(self, hostname, username, password, fail=None):
        self.host = hostname
        self.model = "Reolink Video Doorbell PoE"
        self.channels = [0]
        self.baichuan = FakeBaichuan()
        self.fail = fail
        self.visitor = False
        self.motion = False
        self.logged_out = False

    async def get_host_data(self):
        if self.fail is not None:
            raise self.fail

    async def logout(self):
        self.logged_out = True

    def visitor_detected(self, channel):
        return self.visitor

    def motion_detected(self, channel):
        return self.motion

    def ai_detected(self, channel, object_type):
        return False

    def camera_name(self, channel):
        return "Doorbell"


class HostFactory:
    """Hands out FakeHost objects, failing the first `failures` connections."""

    def __init__(self, failures=0, failure=None):
        self.failures = failures
        self.failure = failure or asyncio.TimeoutError()
        self.hosts = []

    def __call__(self, hostname, username, password):
        fail = self.failure if len(self.hosts) < self.failures else None
        host = FakeHost(hostname, username, password, fail)
        self.hosts.append(host)
        return host


class ReolinkSupervisionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = _load_module()

    def setUp(self):
        patcher = mock.patch.multiple(self.mod,
                                      SUPERVISE_INTERVAL=0.01,
                                      EVENTS_INACTIVE_GRACE=0.05,
                                      RETRY_MIN=0.01,
                                      RETRY_MAX=0.02)
        patcher.start()
        self.addCleanup(patcher.stop)

        self.loop = asyncio.new_event_loop()
        self.addCleanup(self.loop.close)
        self.sent = []

    def make_client(self, factory):
        client = self.mod.ReolinkClient(host_factory=factory)
        client.event_loop = self.loop
        client.send_message = lambda data: self.sent.append(json.loads(data))
        return client

    def run_until(self, predicate, timeout=2.0):
        async def wait():
            deadline = self.loop.time() + timeout
            while not predicate():
                if self.loop.time() > deadline:
                    raise AssertionError("condition not reached")
                await asyncio.sleep(0.005)
        self.loop.run_until_complete(wait())

    def tearDown(self):
        for task in asyncio.all_tasks(self.loop):
            task.cancel()
        self.loop.run_until_complete(asyncio.sleep(0))

    def detections(self):
        return [m for m in self.sent if m.get("event") == "detection"]

    def test_camera_unreachable_at_registration_is_retried(self):
        factory = HostFactory(failures=3)
        client = self.make_client(factory)
        client.register("192.168.30.78", "admin", "secret", "visitor")

        cam = client.cameras["192.168.30.78"]
        self.run_until(lambda: cam.host is not None)

        self.assertEqual(len(factory.hosts), 4)
        errors = [m for m in self.sent if m.get("status") == "error"]
        self.assertEqual([m["code"] for m in errors], ["camera_connect_timeout"] * 3)
        self.assertTrue(all(h.logged_out for h in factory.hosts[:3]))

    def test_dead_subscription_triggers_a_fresh_connection(self):
        factory = HostFactory()
        client = self.make_client(factory)
        client.register("192.168.30.78", "admin", "secret", "visitor")
        cam = client.cameras["192.168.30.78"]
        self.run_until(lambda: cam.host is not None)

        first = cam.host
        first.baichuan.events_active = False
        self.run_until(lambda: cam.host is not None and cam.host is not first)

        self.assertTrue(first.baichuan.unsubscribed)
        self.assertTrue(first.logged_out)

    def test_one_event_per_doorbell_press(self):
        factory = HostFactory()
        client = self.make_client(factory)
        client.register("192.168.30.78", "admin", "secret", "visitor")
        cam = client.cameras["192.168.30.78"]
        self.run_until(lambda: cam.host is not None)
        host = cam.host

        host.visitor = True
        host.baichuan.push()
        host.baichuan.push()
        host.visitor = False
        host.baichuan.push()
        host.visitor = True
        host.baichuan.push()

        self.assertEqual([m["event_type"] for m in self.detections()], ["visitor", "visitor"])
        self.assertEqual(self.detections()[0]["hostname"], "192.168.30.78")

    def test_state_already_raised_at_connection_is_not_an_event(self):
        factory = HostFactory()

        def motion_already_raised(*args):
            host = factory(*args)
            host.motion = True
            return host

        client = self.make_client(motion_already_raised)
        client.register("192.168.30.78", "admin", "secret", "motion")
        cam = client.cameras["192.168.30.78"]

        self.run_until(lambda: cam.host is not None)
        cam.host.baichuan.push()
        self.assertEqual(self.detections(), [])

    def test_event_types_share_one_session_and_only_registered_ones_are_sent(self):
        factory = HostFactory()
        client = self.make_client(factory)
        client.register("192.168.30.78", "admin", "secret", "visitor")
        client.register("192.168.30.78", "admin", "secret", "motion")
        cam = client.cameras["192.168.30.78"]
        self.run_until(lambda: cam.host is not None)

        self.assertEqual(len(factory.hosts), 1)

        cam.host.visitor = True
        cam.host.motion = True
        cam.host.baichuan.push()
        self.assertEqual(sorted(m["event_type"] for m in self.detections()), ["motion", "visitor"])

    def test_events_from_a_replaced_session_are_ignored(self):
        factory = HostFactory()
        client = self.make_client(factory)
        client.register("192.168.30.78", "admin", "secret", "visitor")
        cam = client.cameras["192.168.30.78"]
        self.run_until(lambda: cam.host is not None)

        old = cam.host
        old.baichuan.events_active = False
        self.run_until(lambda: cam.host is not None and cam.host is not old)

        old.visitor = True
        old.baichuan.push()
        self.assertEqual(self.detections(), [])

    def test_watchdog_exits_when_event_loop_stalls(self):
        client = self.make_client(HostFactory())
        client.last_heartbeat -= self.mod.WATCHDOG_TIMEOUT + 1

        with mock.patch.object(self.mod.os, "_exit") as exit_mock:
            client.read_timeout()
        exit_mock.assert_called_once_with(1)

    def test_watchdog_quiet_while_heartbeat_is_fresh(self):
        client = self.make_client(HostFactory())
        with mock.patch.object(self.mod.os, "_exit") as exit_mock:
            client.read_timeout()
        exit_mock.assert_not_called()

    def test_library_warnings_are_forwarded_with_passwords_redacted(self):
        client = self.make_client(HostFactory())
        client.cameras["192.168.30.78"] = self.mod.Camera("192.168.30.78", "admin", "s3cr3t")

        logged = []
        handler = self.mod._LibraryLogForwarder(client)
        with mock.patch.object(self.mod, "cWarningDom", lambda dom: logged.append):
            handler.emit(logging.LogRecord("reolink_aio", logging.WARNING, __file__, 1,
                                           "login failed with password %s", ("s3cr3t",), None))

        self.assertEqual(len(logged), 1)
        self.assertNotIn("s3cr3t", logged[0])
        self.assertIn("<redacted>", logged[0])


if __name__ == "__main__":
    unittest.main()
