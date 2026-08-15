# T1.16 — regression tests for CalaosClient pending-future lifetime and
# dead-socket handling (client.py).
#
# Run with: python3 -m unittest discover -s tests/python -p 'test_t116_*.py'
# (also compatible with pytest). No third-party dependency required: the
# `websockets` module is stubbed before importing calaos_mcp.client.

import asyncio
import os
import sys
import types
import unittest

_PKG_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "src", "bin", "calaos_mcp", "python",
)
sys.path.insert(0, os.path.abspath(_PKG_DIR))

# Stub `websockets` (not installed in the test env; annotations are lazy
# thanks to `from __future__ import annotations` so a bare module is enough).
if "websockets" not in sys.modules:
    sys.modules["websockets"] = types.ModuleType("websockets")

from calaos_mcp.client import CalaosClient  # noqa: E402


class FakeWs:
    def __init__(self):
        self.sent = []
        self.fail_send = False

    async def send(self, raw):
        if self.fail_send:
            raise ConnectionError("boom")
        self.sent.append(raw)


def run(coro):
    return asyncio.new_event_loop().run_until_complete(coro)


class PendingFutureLifetimeTest(unittest.TestCase):
    def _connected_client(self):
        c = CalaosClient()
        c._ws = FakeWs()
        c._ready.set()
        return c

    def test_request_timeout_pops_pending(self):
        """On wait_for timeout the _pending entry must be removed (leak fix)."""
        async def scenario():
            c = self._connected_client()
            orig_wait_for = asyncio.wait_for

            async def fast_wait_for(fut, timeout):
                return await orig_wait_for(fut, timeout=0.01)

            asyncio.wait_for = fast_wait_for
            try:
                with self.assertRaises(asyncio.TimeoutError):
                    await c._request("get_home")
            finally:
                asyncio.wait_for = orig_wait_for
            self.assertEqual(c._pending, {})

        run(scenario())

    def test_send_failure_pops_pending(self):
        """If ws.send raises, the freshly-registered future must not leak."""
        async def scenario():
            c = self._connected_client()
            c._ws.fail_send = True
            with self.assertRaises(ConnectionError):
                await c._request("get_home")
            self.assertEqual(c._pending, {})

        run(scenario())

    def test_disconnect_fails_outstanding_futures_and_clears_dict(self):
        """_on_disconnect must resolve every in-flight future with an error."""
        async def scenario():
            c = self._connected_client()
            fut1 = asyncio.get_event_loop().create_future()
            fut2 = asyncio.get_event_loop().create_future()
            c._pending = {"mcp-1": fut1, "mcp-2": fut2}

            c._on_disconnect()

            self.assertEqual(c._pending, {})
            for fut in (fut1, fut2):
                self.assertTrue(fut.done())
                self.assertIsInstance(fut.exception(), ConnectionError)

        run(scenario())

    def test_disconnect_clears_ready_and_ws(self):
        """After a close (clean or not) sends must be refused, not attempted."""
        async def scenario():
            c = self._connected_client()
            c._on_disconnect()
            self.assertFalse(c._ready.is_set())
            self.assertIsNone(c._ws)
            with self.assertRaises(RuntimeError):
                await c._request("get_home")

        run(scenario())

    def test_clean_close_runs_disconnect_handler(self):
        """_connect_once must invoke _on_disconnect even when the WS loop
        ends without error (clean server-side close)."""
        async def scenario():
            c = CalaosClient()
            called = []
            c._on_disconnect = lambda: called.append(True)

            class _Ctx:
                async def __aenter__(self):
                    raise ConnectionError("refused")

                async def __aexit__(self, *a):
                    return False

            ws_mod = sys.modules["websockets"]
            ws_mod.connect = lambda url: _Ctx()

            cfg = types.SimpleNamespace(api_url="ws://test", service_token="t")
            import calaos_mcp.client as client_mod
            orig_get_config = client_mod.get_config
            client_mod.get_config = lambda: cfg
            try:
                with self.assertRaises(ConnectionError):
                    await c._connect_once()
            finally:
                client_mod.get_config = orig_get_config
            self.assertEqual(called, [True])

        run(scenario())


if __name__ == "__main__":
    unittest.main()
