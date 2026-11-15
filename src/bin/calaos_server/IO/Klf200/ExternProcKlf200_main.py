#!/bin/env python3
#
# Calaos external process for the VELUX KLF 200 gateway.
#
# One process per calaos_server, one pyvlx gateway session per KLF 200 host.
# The C++ side (IO/Klf200/Klf200Ctrl.cpp) registers the nodes it is interested
# in and sends movement commands; this side pushes the node state back every
# time the gateway reports a change, and polls the rain sensor limitation of
# the windows that asked for it.
#
# Wire (JSON objects, one per ExternProc frame) -- the C++ mirror is
# IO/Klf200/Klf200Wire.h, keep the two in sync:
#
#   server -> here
#     {"action": "register", "host": H, "password": P, "node_id": N, "rain": bool}
#     {"action": "set_position", "host": H, "node_id": N, "position": 0..100}
#     {"action": "stop", "host": H, "node_id": N}
#
#   here -> server
#     {"status": "connected"}                                   process ready
#     {"status": "gateway", "host": H, "connected": bool}       KLF 200 session
#     {"status": "error", "host": H, "code": <code>, "node_id": N?, "message": ...}
#     {"event": "node", "host": H, "node_id": N, "position": -1|0..100,
#      "moving": "up"|"down"|"stop", "node_type": <pyvlx class name>,
#      "name": <node name>, "rain_sensor": bool}
#     {"event": "rain", "host": H, "node_id": N, "rain": bool}
#
# Position convention, identical on both ends and in the KLF 200 itself:
# 0 = fully open (shutter up, window open), 100 = fully closed.
#
# Threading: the ExternProc loop (select on the unix socket) runs in the main
# thread, pyvlx runs on an asyncio loop in a daemon thread, exactly like
# IO/Reolink/ExternProcReolink_main.py. Commands cross with
# run_coroutine_threadsafe(); state crosses back through send_message(),
# which takes a lock and is safe to call from the loop thread.

import argparse
import asyncio
import json
import logging
import threading

from calaos_extern_proc import (ExternProcClient, cCriticalDom, cDebugDom,
                                cErrorDom, cInfoDom, cWarningDom,
                                configure_logger)

try:
    import pyvlx
    from pyvlx import OpeningDevice, Position, PyVLX, PyVLXException, Window
except ImportError:  # pragma: no cover - the configure probe refuses to build without it
    pyvlx = None
    OpeningDevice = Position = PyVLX = PyVLXException = Window = None

LOG_DOMAIN = "klf200"

# The KLF 200 reports rain by putting a minimum-position limitation on the
# window (it may only stay slightly open for ventilation). The value depends
# on the model: 89, 91, 93 (most windows) or 100 (VELUX GPU) have been seen,
# so any limitation at or above 89% means rain. Same threshold as the Home
# Assistant integration (homeassistant/components/velux/binary_sensor.py),
# where these numbers come from. The gateway never pushes that change, so it
# has to be polled.
RAIN_LIMITATION_MIN_PERCENT = 89

# Reconnection backoff to the gateway, in seconds.
RECONNECT_DELAY_FIRST = 5
RECONNECT_DELAY_MAX = 120

# The error codes the C++ end knows (Klf200Wire::isKnownErrorCode). A code
# outside this list is published as "unspecified" over there, so add it on
# both sides.
ERR_GATEWAY_CONNECT_FAILED = "gateway_connect_failed"
ERR_GATEWAY_NOT_CONNECTED = "gateway_not_connected"
ERR_NODE_NOT_FOUND = "node_not_found"
ERR_NODE_UNSUPPORTED = "node_unsupported"
ERR_COMMAND_FAILED = "command_failed"


def parse_command(data):
    """Validate one decoded message from calaos_server.

    Returns (command, None) with a normalized dict, or (None, reason) when the
    message must be dropped. Pure: no I/O, no pyvlx, so tests/python can pin
    the grammar without a gateway.
    """
    if not isinstance(data, dict):
        return None, "message is not a JSON object"

    action = data.get("action")
    if action not in ("register", "set_position", "stop"):
        return None, f"unknown action: {action!r}"

    host = data.get("host")
    if not isinstance(host, str) or not host:
        return None, "missing host"

    node_id = data.get("node_id")
    try:
        node_id = int(node_id)
    except (TypeError, ValueError):
        return None, f"invalid node_id: {node_id!r}"
    if node_id < 0:
        return None, f"invalid node_id: {node_id!r}"

    cmd = {"action": action, "host": host, "node_id": node_id}

    if action == "register":
        password = data.get("password")
        if not isinstance(password, str) or not password:
            return None, "missing password"
        cmd["password"] = password
        cmd["rain"] = bool(data.get("rain", False))
    elif action == "set_position":
        position = data.get("position")
        try:
            position = int(position)
        except (TypeError, ValueError):
            return None, f"invalid position: {position!r}"
        cmd["position"] = max(0, min(100, position))

    return cmd, None


def redact(data):
    """Copy of a decoded message with the password hidden, for the logs."""
    if not isinstance(data, dict):
        return data
    return {k: ("<redacted>" if k == "password" else v) for k, v in data.items()}


def moving_state(node):
    """'up' while opening, 'down' while closing, 'stop' otherwise."""
    if getattr(node, "is_opening", False):
        return "up"
    if getattr(node, "is_closing", False):
        return "down"
    return "stop"


def node_position_percent(node):
    """Current position in percent, or -1 while the gateway has not said."""
    try:
        pos = node.get_position() if hasattr(node, "get_position") else node.position
    except Exception:  # pragma: no cover - defensive, pyvlx interpolation
        pos = getattr(node, "position", None)
    if pos is None or not getattr(pos, "known", False):
        return -1
    return int(pos.position_percent)


def node_state_message(host, node):
    """The {"event": "node", ...} frame for one node."""
    return {
        "event": "node",
        "host": host,
        "node_id": int(node.node_id),
        "position": node_position_percent(node),
        "moving": moving_state(node),
        "node_type": type(node).__name__,
        "name": str(getattr(node, "name", "")),
        "rain_sensor": bool(getattr(node, "rain_sensor", False)),
    }


def rain_from_limitation(min_percent):
    """Whether a minimum-position limitation means the rain sensor is wet."""
    return int(min_percent) >= RAIN_LIMITATION_MIN_PERCENT


def node_has_rain_sensor(node):
    return Window is not None and isinstance(node, Window) and bool(getattr(node, "rain_sensor", False))


class Gateway:
    """One KLF 200 session. Lives on the asyncio loop thread only."""

    def __init__(self, client, host, password, rain_poll_interval):
        self.client = client
        self.host = host
        self.password = password
        self.rain_poll_interval = rain_poll_interval
        self.pyvlx = None
        self.connected = False
        self.registrations = {}   # node_id -> {"rain": bool}
        self.attached = set()     # node ids whose update callback is installed
        self.rain_state = {}      # node_id -> last published rain bool
        self._closed = asyncio.Event()
        self._opened = asyncio.Event()
        self._task = None
        self._rain_task = None

    # -- session -----------------------------------------------------------

    def start(self):
        if self._task is None:
            self._task = asyncio.create_task(self._run())

    async def _run(self):
        delay = RECONNECT_DELAY_FIRST
        while True:
            try:
                await self._connect()
                delay = RECONNECT_DELAY_FIRST
                await self._closed.wait()
                cWarningDom(LOG_DOMAIN)("KLF 200 %s: connection lost, reconnecting", self.host)
            except asyncio.CancelledError:
                raise
            except Exception as e:  # PyVLXException, OSError, ssl errors...
                cErrorDom(LOG_DOMAIN)("KLF 200 %s: connection failed: %s", self.host, e)
                self.client.send_error(self.host, ERR_GATEWAY_CONNECT_FAILED, str(e))
                self._drop_session()
            self._set_connected(False)
            await self._wait_retry(delay)
            delay = min(delay * 2, RECONNECT_DELAY_MAX)

    async def _wait_retry(self, delay):
        """Sleep before the next attempt, but wake up as soon as pyvlx reopens
        the session on its own: its heartbeat keeps running after a drop and
        reconnects through send_frame() -> ensure_connected(). Without this,
        commands would be refused for the rest of a long backoff while the
        session is in fact up again."""
        try:
            await asyncio.wait_for(self._opened.wait(), timeout=delay)
        except asyncio.TimeoutError:
            pass

    def _drop_session(self):
        """Close what a failed attempt left open. pyvlx.connect() opens the
        TLS connection before it authenticates and does not close it when a
        later step (password, version, house monitor) fails: the next
        connect() would overwrite the transport and leak the socket, one per
        retry, and the KLF 200 only accepts a couple of sessions."""
        if self.pyvlx is None:
            return
        try:
            self.pyvlx.connection.disconnect(notify_callbacks=False)
        except Exception as e:  # pragma: no cover - defensive
            cDebugDom(LOG_DOMAIN)("KLF 200 %s: closing the failed session: %s", self.host, e)
        self._opened.clear()

    async def _connect(self):
        self._closed.clear()
        if self.pyvlx is None:
            self.pyvlx = PyVLX(host=self.host, password=self.password)
            self.pyvlx.connection.register_connection_closed_cb(self._on_connection_closed)
            self.pyvlx.connection.register_connection_opened_cb(self._on_connection_opened)
        if self.pyvlx.get_connected():
            # pyvlx's heartbeat reopened the session while this loop was
            # waiting: connecting again would open a second TLS session.
            cInfoDom(LOG_DOMAIN)("KLF 200 %s: session reopened by pyvlx", self.host)
        else:
            cInfoDom(LOG_DOMAIN)("KLF 200 %s: connecting", self.host)
            await self.pyvlx.connect()
        await self.pyvlx.load_nodes()
        cInfoDom(LOG_DOMAIN)("KLF 200 %s: connected, %d nodes", self.host, len(self.pyvlx.nodes))
        self._set_connected(True)
        for node_id in list(self.registrations):
            await self._attach(node_id)
        if self._rain_task is None:
            self._rain_task = asyncio.create_task(self._rain_poll())

    async def _on_connection_closed(self):
        self._opened.clear()
        self._closed.set()

    async def _on_connection_opened(self):
        self._opened.set()

    def _set_connected(self, connected):
        if connected == self.connected:
            return
        self.connected = connected
        self.attached.clear()
        self.client.send_message(json.dumps({"status": "gateway", "host": self.host, "connected": connected}))

    async def shutdown(self):
        for task in (self._rain_task, self._task):
            if task is not None:
                task.cancel()
        if self.pyvlx is not None and self.pyvlx.get_connected():
            # Not pyvlx.disconnect(): that one reboots the gateway, which
            # takes it offline for a couple of minutes at every calaos
            # restart. Only close the session cleanly.
            try:
                if self.pyvlx.klf200.house_status_monitor_enabled:
                    await self.pyvlx.klf200.house_status_monitor_disable(pyvlx=self.pyvlx, timeout=5)
            except Exception as e:
                cDebugDom(LOG_DOMAIN)("KLF 200 %s: monitor disable failed on shutdown: %s", self.host, e)
            self.pyvlx.connection.disconnect(notify_callbacks=False)

    # -- nodes -------------------------------------------------------------

    def _find_node(self, node_id):
        if self.pyvlx is None or not self.connected:
            return None
        try:
            return self.pyvlx.nodes[node_id]
        except KeyError:
            return None

    async def _attach(self, node_id):
        node = self._find_node(node_id)
        if node is None:
            cErrorDom(LOG_DOMAIN)("KLF 200 %s: node %d not found on the gateway", self.host, node_id)
            self.client.send_error(self.host, ERR_NODE_NOT_FOUND, "node not found", node_id)
            return
        if not isinstance(node, OpeningDevice):
            cErrorDom(LOG_DOMAIN)("KLF 200 %s: node %d (%s) is not an opening device",
                                  self.host, node_id, type(node).__name__)
            self.client.send_error(self.host, ERR_NODE_UNSUPPORTED, type(node).__name__, node_id)
            return
        if node_id not in self.attached:
            if self._node_updated not in node.device_updated_cbs:
                node.register_device_updated_cb(self._node_updated)
            self.attached.add(node_id)
        await self._node_updated(node)

    async def _node_updated(self, node):
        if node.node_id not in self.registrations:
            return
        self.client.send_message(json.dumps(node_state_message(self.host, node)))

    async def register(self, node_id, rain):
        reg = self.registrations.setdefault(node_id, {"rain": False})
        reg["rain"] = reg["rain"] or rain
        if rain:
            # A rain IO registering does not know the state, and may be a new
            # IO for a node this process already reported (config reload):
            # forget the last value so the next reading reaches it even when
            # it did not change.
            self.rain_state.pop(node_id, None)
        if self.connected:
            await self._attach(node_id)
            if rain:
                await self._poll_rain_node(node_id)

    def _command_target(self, node_id, what):
        """The node a command addresses, or None (an error frame is sent)."""
        if not self.connected:
            cWarningDom(LOG_DOMAIN)("KLF 200 %s: %s on node %d while the gateway is not connected",
                                    self.host, what, node_id)
            self.client.send_error(self.host, ERR_GATEWAY_NOT_CONNECTED, "gateway not connected", node_id)
            return None
        node = self._find_node(node_id)
        if node is None or not isinstance(node, OpeningDevice):
            cWarningDom(LOG_DOMAIN)("KLF 200 %s: %s on unknown node %d", self.host, what, node_id)
            self.client.send_error(self.host, ERR_NODE_NOT_FOUND, "node not found", node_id)
            return None
        return node

    async def set_position(self, node_id, percent):
        node = self._command_target(node_id, "set_position")
        if node is None:
            return
        cDebugDom(LOG_DOMAIN)("KLF 200 %s: node %d -> %d%%", self.host, node_id, percent)
        try:
            await node.set_position(Position(position_percent=percent), wait_for_completion=False)
        except Exception as e:
            cErrorDom(LOG_DOMAIN)("KLF 200 %s: set_position on node %d failed: %s", self.host, node_id, e)
            self.client.send_error(self.host, ERR_COMMAND_FAILED, str(e), node_id)

    async def stop(self, node_id):
        node = self._command_target(node_id, "stop")
        if node is None:
            return
        cDebugDom(LOG_DOMAIN)("KLF 200 %s: node %d stop", self.host, node_id)
        try:
            await node.stop(wait_for_completion=False)
        except Exception as e:
            cErrorDom(LOG_DOMAIN)("KLF 200 %s: stop on node %d failed: %s", self.host, node_id, e)
            self.client.send_error(self.host, ERR_COMMAND_FAILED, str(e), node_id)

    # -- rain sensor -------------------------------------------------------

    async def _rain_poll(self):
        while True:
            try:
                await self._poll_rain_once()
            except asyncio.CancelledError:
                raise
            except Exception as e:
                cWarningDom(LOG_DOMAIN)("KLF 200 %s: rain poll failed: %s", self.host, e)
            await asyncio.sleep(self.rain_poll_interval)

    async def _poll_rain_once(self):
        if not self.connected:
            return
        for node_id, reg in list(self.registrations.items()):
            if reg["rain"]:
                await self._poll_rain_node(node_id)

    async def _poll_rain_node(self, node_id):
        node = self._find_node(node_id)
        if node is None:
            return
        if not node_has_rain_sensor(node):
            if node_id not in self.rain_state:
                cWarningDom(LOG_DOMAIN)("KLF 200 %s: node %d has no rain sensor, reporting dry",
                                        self.host, node_id)
            self._publish_rain(node_id, False)
            return
        if node.is_moving():
            return  # a status request mid-travel can interrupt the motion
        limitation = await node.get_limitation_min()
        rain = rain_from_limitation(limitation.position_percent)
        cDebugDom(LOG_DOMAIN)("KLF 200 %s: node %d limitation min=%d%% rain=%s",
                              self.host, node_id, limitation.position_percent, rain)
        self._publish_rain(node_id, rain)

    def _publish_rain(self, node_id, rain):
        if self.rain_state.get(node_id) == rain:
            return
        self.rain_state[node_id] = rain
        self.client.send_message(json.dumps({"event": "rain", "host": self.host,
                                             "node_id": node_id, "rain": rain}))


class Klf200Client(ExternProcClient):
    def __init__(self):
        super().__init__()
        self.event_loop = None
        self.event_loop_thread = None
        self.gateways = {}   # host -> Gateway, loop thread only
        self.rain_poll_interval = 300
        self.cli_list = False
        self.cli_host = None
        self.cli_password = None

    # -- arguments ---------------------------------------------------------

    def parse_arguments(self):
        parser = argparse.ArgumentParser(description="Calaos VELUX KLF 200 driver")
        parser.add_argument("--socket", help="ExternProc socket path")
        parser.add_argument("--namespace", help="ExternProc namespace")
        parser.add_argument("--list", action="store_true",
                            help="List the nodes of a gateway and exit (needs --host and --password)")
        parser.add_argument("--host", help="KLF 200 host (with --list)")
        parser.add_argument("--password", help="KLF 200 password (with --list)")
        parser.add_argument("--rain-poll", type=int, default=300,
                            help="Rain sensor poll interval in seconds (default 300)")
        args, _unknown = parser.parse_known_args()

        if args.list:
            if not args.host or not args.password:
                parser.error("--list needs --host and --password")
            if args.socket or args.namespace:
                parser.error("--list cannot be combined with --socket or --namespace")
        elif not args.socket or not args.namespace:
            parser.error("--socket and --namespace are required")

        self.cli_list = args.list
        self.cli_host = args.host
        self.cli_password = args.password
        self.rain_poll_interval = max(10, args.rain_poll)
        if not args.list:
            self.sockpath = args.socket
            self.name = args.namespace

    # -- lifecycle ---------------------------------------------------------

    def setup(self):
        self.parse_arguments()

        if pyvlx is None:
            cCriticalDom(LOG_DOMAIN)("python module pyvlx is not installed (pip3 install pyvlx)")
            return False

        # pyvlx logs every frame at DEBUG; keep its logger quiet unless calaos
        # itself runs at the debug level.
        if logging.getLogger().getEffectiveLevel() > logging.DEBUG:
            logging.getLogger("pyvlx").setLevel(logging.WARNING)

        if self.cli_list:
            return True

        if not self.connect_socket():
            cCriticalDom(LOG_DOMAIN)("Failed to connect to socket")
            return False

        self.event_loop = asyncio.new_event_loop()

        def run_event_loop():
            asyncio.set_event_loop(self.event_loop)
            self.event_loop.run_forever()

        self.event_loop_thread = threading.Thread(target=run_event_loop, daemon=True)
        self.event_loop_thread.start()

        self.send_message(json.dumps({"status": "connected"}))
        return True

    def list_nodes(self):
        """--list mode: print the nodes of one gateway, then exit."""
        # Imported here, not at the top: tests/python stubs a bare pyvlx
        # module, without the api subpackage.
        from pyvlx.api.status_request import StatusRequest

        async def _status(vlx, node):
            """Ask the actuator for its state; pyvlx's NodeUpdater stores the
            position on the node. The gateway reports every position as
            unknown right after connect, and pyvlx's own heartbeat would only
            ask 30 s later."""
            if node.is_moving():
                return "moving"  # a status request mid-travel can interrupt the motion
            req = StatusRequest(vlx, node.node_id)
            await req.do_api_call()
            await asyncio.sleep(0.5)  # same pacing as the pyvlx heartbeat
            return "ok" if req.success else "no_reply"

        async def _list():
            vlx = PyVLX(host=self.cli_host, password=self.cli_password)
            await vlx.connect()
            await vlx.load_nodes()
            print(f"KLF 200 {self.cli_host}: {len(vlx.nodes)} node(s)")
            for node in vlx.nodes:
                extra = ""
                if isinstance(node, OpeningDevice):
                    before = node_position_percent(node)
                    status = await _status(vlx, node)
                    extra = (f" position={node_position_percent(node)}%"
                             f" (at load: {before}%, status request: {status})")
                if node_has_rain_sensor(node):
                    extra += " rain_sensor=yes"
                    try:
                        limitation = await node.get_limitation_min()
                        rain = rain_from_limitation(limitation.position_percent)
                        extra += f" limitation_min={limitation.position_percent}% rain={'yes' if rain else 'no'}"
                    except Exception as e:
                        extra += f" rain=? ({e})"
                print(f"  node_id={node.node_id:<3} type={type(node).__name__:<18} name={node.name!r}{extra}")
            vlx.connection.disconnect(notify_callbacks=False)

        try:
            asyncio.run(_list())
            return 0
        except Exception as e:
            cCriticalDom(LOG_DOMAIN)("KLF 200 %s: %s", self.cli_host, e)
            return 1

    def stop(self):
        if self.event_loop is not None and not self.event_loop.is_closed():
            async def _shutdown():
                for gw in list(self.gateways.values()):
                    await gw.shutdown()
            try:
                asyncio.run_coroutine_threadsafe(_shutdown(), self.event_loop).result(timeout=10)
            except Exception as e:
                cDebugDom(LOG_DOMAIN)("shutdown: %s", e)
            self.event_loop.call_soon_threadsafe(self.event_loop.stop)
        return super().stop()

    # -- wire --------------------------------------------------------------

    def send_error(self, host, code, message, node_id=None):
        msg = {"status": "error", "host": host, "code": code, "message": str(message)}
        if node_id is not None:
            msg["node_id"] = int(node_id)
        self.send_message(json.dumps(msg))

    def message_received(self, message):
        try:
            data = json.loads(message)
        except Exception as e:
            cErrorDom(LOG_DOMAIN)("Failed to parse JSON message (%d bytes): %s", len(message), e)
            return

        cDebugDom(LOG_DOMAIN)("Received message: %s", json.dumps(redact(data)))

        cmd, reason = parse_command(data)
        if cmd is None:
            cWarningDom(LOG_DOMAIN)("Dropping message: %s", reason)
            return

        if self.event_loop is None or self.event_loop.is_closed():
            cErrorDom(LOG_DOMAIN)("Event loop not running, dropping %s", cmd["action"])
            return

        asyncio.run_coroutine_threadsafe(self.dispatch(cmd), self.event_loop)

    async def dispatch(self, cmd):
        """Runs on the loop thread."""
        host = cmd["host"]
        try:
            if cmd["action"] == "register":
                gw = self.gateways.get(host)
                if gw is None:
                    gw = Gateway(self, host, cmd["password"], self.rain_poll_interval)
                    self.gateways[host] = gw
                    gw.start()
                elif gw.password != cmd["password"]:
                    cWarningDom(LOG_DOMAIN)("KLF 200 %s: node %d registered with a different password, "
                                            "keeping the first one", host, cmd["node_id"])
                await gw.register(cmd["node_id"], cmd["rain"])
                return

            gw = self.gateways.get(host)
            if gw is None:
                cWarningDom(LOG_DOMAIN)("KLF 200 %s: %s before any register, dropped", host, cmd["action"])
                return
            if cmd["action"] == "set_position":
                await gw.set_position(cmd["node_id"], cmd["position"])
            elif cmd["action"] == "stop":
                await gw.stop(cmd["node_id"])
        except Exception as e:
            cErrorDom(LOG_DOMAIN)("KLF 200 %s: %s failed: %s", host, cmd["action"], e)


if __name__ == "__main__":
    configure_logger()
    cInfoDom(LOG_DOMAIN)("Starting VELUX KLF 200 driver")

    client = Klf200Client()
    if not client.setup():
        cCriticalDom(LOG_DOMAIN)("Failed to setup Klf200Client")
        exit(1)

    if client.cli_list:
        exit(client.list_nodes())

    client.run(200)
