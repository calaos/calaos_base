#!/usr/bin/env python3

from calaos_extern_proc import cDebugDom, cInfoDom, cErrorDom, cCriticalDom, cWarningDom, configure_logger, ExternProcClient
from reolink_aio.api import Host
import asyncio
import argparse
import json
import logging
import os
import threading
import time
from datetime import datetime

# reolink_aio already keeps the Baichuan session alive and re-subscribes on
# its own; this layer only has to recover what the library gives up on, so it
# polls slowly and never gives up on a camera.
SUPERVISE_INTERVAL = 60
EVENTS_INACTIVE_GRACE = 180
CONNECT_TIMEOUT = 30
CALL_TIMEOUT = 30
RETRY_MIN = 10
RETRY_MAX = 300

HEARTBEAT_INTERVAL = 5
WATCHDOG_TIMEOUT = 120

AI_TYPES = ("face", "person", "vehicle", "pet", "package", "cry")


def detection_state(host, channel, event_type):
    if event_type == "motion":
        return bool(host.motion_detected(channel))
    if event_type == "visitor":
        return bool(host.visitor_detected(channel))
    if event_type in AI_TYPES:
        return bool(host.ai_detected(channel, event_type))
    return False


class _LibraryLogForwarder(logging.Handler):
    """Forwards reolink_aio warnings to the calaos log, camera passwords redacted."""

    def __init__(self, client):
        super().__init__(level=logging.WARNING)
        self.client = client

    def emit(self, record):
        try:
            text = record.getMessage()
            for secret in self.client.known_passwords():
                text = text.replace(secret, "<redacted>")
            if record.levelno >= logging.ERROR:
                cErrorDom("reolink")(f"reolink_aio: {text}")
            else:
                cWarningDom("reolink")(f"reolink_aio: {text}")
        except Exception:
            pass


class Camera:
    def __init__(self, hostname, username, password):
        self.hostname = hostname
        self.username = username
        self.password = password
        self.event_types = set()
        self.host = None
        self.task = None
        self.states = {}
        self.inactive_since = None


class ReolinkClient(ExternProcClient):
    def __init__(self, host_factory=Host):
        super().__init__()
        self.host_factory = host_factory
        self.cameras = {}  # hostname -> Camera, only touched from the event loop thread
        self.event_loop = None
        self.event_loop_thread = None
        self.last_heartbeat = time.monotonic()

    def known_passwords(self):
        return [cam.password for cam in list(self.cameras.values()) if cam.password]

    def parse_arguments(self):
        parser = argparse.ArgumentParser(description='Calaos Reolink Extension')
        parser.add_argument('--socket', help='Socket path')
        parser.add_argument('--namespace', help='Namespace')
        args = parser.parse_args()

        if not args.socket or not args.namespace:
            parser.error('Must specify both --socket and --namespace')

        self.sockpath = args.socket
        self.name = args.namespace

    def setup(self):
        self.parse_arguments()

        if not self.connect_socket():
            cCriticalDom("reolink")("Failed to connect to socket")
            return False

        lib_logger = logging.getLogger("reolink_aio")
        lib_logger.addHandler(_LibraryLogForwarder(self))
        lib_logger.setLevel(logging.WARNING)
        lib_logger.propagate = False

        self.event_loop = asyncio.new_event_loop()
        self.event_loop_thread = threading.Thread(target=self._run_event_loop, daemon=True)
        self.event_loop_thread.start()
        asyncio.run_coroutine_threadsafe(self.heartbeat(), self.event_loop)

        self.send_message(json.dumps({"status": "connected", "message": "Reolink client ready"}))
        return True

    def _run_event_loop(self):
        asyncio.set_event_loop(self.event_loop)
        self.event_loop.run_forever()

    async def heartbeat(self):
        while True:
            self.last_heartbeat = time.monotonic()
            await asyncio.sleep(HEARTBEAT_INTERVAL)

    def watchdog_expired(self):
        if self.event_loop_thread is not None and not self.event_loop_thread.is_alive():
            return True
        return time.monotonic() - self.last_heartbeat > WATCHDOG_TIMEOUT

    def read_timeout(self):
        # Exiting hands recovery to calaos_server: it respawns the process and
        # re-sends every registration once the new one says "connected".
        if self.watchdog_expired():
            cCriticalDom("reolink")("Event loop unresponsive, exiting so calaos_server restarts the process")
            os._exit(1)

    def register(self, hostname, username, password, event_type):
        cam = self.cameras.get(hostname)
        if cam is None:
            cam = Camera(hostname, username, password)
            self.cameras[hostname] = cam
        elif (cam.username, cam.password) != (username, password):
            cInfoDom("reolink")(f"Credentials changed for camera {hostname}, reconnecting")
            cam.username, cam.password = username, password
            self.event_loop.create_task(self.disconnect(cam))

        cam.event_types.add(event_type)
        if cam.host is not None:
            self.prime_states(cam)

        if cam.task is None or cam.task.done():
            cam.task = self.event_loop.create_task(self.supervise(cam))

    async def supervise(self, cam):
        retry_delay = RETRY_MIN
        while True:
            try:
                if cam.host is None:
                    if await self.connect(cam):
                        retry_delay = RETRY_MIN
                    else:
                        cInfoDom("reolink")(f"Retrying camera {cam.hostname} in {retry_delay}s")
                        await asyncio.sleep(retry_delay)
                        retry_delay = min(retry_delay * 2, RETRY_MAX)
                    continue

                await asyncio.sleep(SUPERVISE_INTERVAL)
                await self.check_events(cam)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                cErrorDom("reolink")(f"Supervision error for camera {cam.hostname}: {type(e).__name__}")
                await self.disconnect(cam)
                await asyncio.sleep(RETRY_MIN)

    async def connect(self, cam):
        cInfoDom("reolink")(f"Connecting to camera {cam.hostname}")
        host = self.host_factory(cam.hostname, cam.username, cam.password)
        try:
            await asyncio.wait_for(host.get_host_data(), CONNECT_TIMEOUT)

            for channel in host.channels:
                host.baichuan.register_callback(f"calaos_{cam.hostname}_ch{channel}",
                                                lambda ch=channel: self.on_event(cam, host, ch),
                                                cmd_id=33, channel=channel)

            await asyncio.wait_for(host.baichuan.subscribe_events(), CALL_TIMEOUT)
        except Exception as e:
            # str(e) comes from the call that authenticated with the camera
            # credentials, and this stdout ends up in the server journal.
            code = "camera_connect_timeout" if isinstance(e, asyncio.TimeoutError) else "camera_connect_failed"
            cWarningDom("reolink")(f"Failed to connect to camera {cam.hostname}: {type(e).__name__}")
            self.send_message(json.dumps({"status": "error", "code": code, "hostname": cam.hostname}))
            await self.close_host(host)
            return False

        cam.host = host
        cam.inactive_since = None
        self.prime_states(cam)
        cInfoDom("reolink")(f"Connected to camera {cam.hostname} ({host.model}), channels {host.channels}, "
                            f"events active: {host.baichuan.events_active}")
        return True

    async def check_events(self, cam):
        host = cam.host
        await asyncio.wait_for(host.baichuan.check_subscribe_events(), CALL_TIMEOUT)

        if host.baichuan.events_active:
            if cam.inactive_since is not None:
                cInfoDom("reolink")(f"Events active again on camera {cam.hostname}")
            cam.inactive_since = None
            return

        now = time.monotonic()
        if cam.inactive_since is None:
            cam.inactive_since = now
            cWarningDom("reolink")(f"Events not active on camera {cam.hostname}")
        elif now - cam.inactive_since > EVENTS_INACTIVE_GRACE:
            cWarningDom("reolink")(f"Events inactive for {int(now - cam.inactive_since)}s on camera {cam.hostname}, reconnecting")
            await self.disconnect(cam)

    async def disconnect(self, cam):
        host, cam.host = cam.host, None
        cam.inactive_since = None
        if host is not None:
            await self.close_host(host)

    async def close_host(self, host):
        try:
            await asyncio.wait_for(host.baichuan.unsubscribe_events(), 10)
        except Exception as e:
            cDebugDom("reolink")(f"Error unsubscribing from {host.host}: {type(e).__name__}")
        try:
            await asyncio.wait_for(host.logout(), 10)
        except Exception as e:
            cDebugDom("reolink")(f"Error logging out from {host.host}: {type(e).__name__}")

    def prime_states(self, cam):
        # A state already raised when we (re)connect is not a new event
        for channel in cam.host.channels:
            for event_type in cam.event_types:
                cam.states[(channel, event_type)] = detection_state(cam.host, channel, event_type)

    def on_event(self, cam, host, channel):
        if host is not cam.host:
            return

        for event_type in sorted(cam.event_types):
            try:
                active = detection_state(host, channel, event_type)
            except Exception as e:
                cDebugDom("reolink")(f"Cannot read {event_type} state on {cam.hostname}: {type(e).__name__}")
                continue

            previous = cam.states.get((channel, event_type), False)
            cam.states[(channel, event_type)] = active
            if not active or previous:
                continue

            cInfoDom("reolink")(f"Event detected on {cam.hostname} channel {channel}: {event_type}")
            self.send_message(json.dumps({
                "event": "detection",
                "hostname": cam.hostname,
                "event_type": event_type,
                "channel": channel,
                "timestamp": datetime.now().isoformat(),
                "camera_name": host.camera_name(channel),
            }))

    def health_status(self):
        return {
            "status": "healthy" if not self.watchdog_expired() else "unhealthy",
            "cameras_count": len(self.cameras),
            "connected_cameras": sum(1 for cam in self.cameras.values() if cam.host is not None),
            "cameras": {
                hostname: {
                    "connected": cam.host is not None,
                    "events_active": cam.host is not None and cam.host.baichuan.events_active,
                }
                for hostname, cam in self.cameras.items()
            },
        }

    def message_received(self, message):
        try:
            msg_data = json.loads(message)
        except Exception as e:
            # Never log the raw message: it may carry credentials
            cErrorDom("reolink")(f"Failed to parse JSON message ({len(message)} bytes): {e}")
            return

        if not isinstance(msg_data, dict):
            cDebugDom("reolink")("Received non-object JSON message")
            return

        redacted = {k: ("<redacted>" if k in ("username", "password") else v) for k, v in msg_data.items()}
        cDebugDom("reolink")(f"Received message: {json.dumps(redacted)}")

        action = msg_data.get("action")
        if action == "register":
            hostname = msg_data.get("hostname")
            username = msg_data.get("username")
            password = msg_data.get("password")
            event_type = msg_data.get("event_type", "motion")

            if not hostname:
                cErrorDom("reolink")("No hostname provided")
                return
            if not username or not password:
                cErrorDom("reolink")("No username or password provided")
                return

            self.event_loop.call_soon_threadsafe(self.register, hostname, username, password, event_type)
        elif action == "health_check":
            self.event_loop.call_soon_threadsafe(
                lambda: self.send_message(json.dumps(self.health_status())))
        else:
            cWarningDom("reolink")(f"Unknown action: {action}")


if __name__ == "__main__":
    configure_logger()
    cInfoDom("reolink")("Starting Reolink control client")

    client = ReolinkClient()
    if client.setup():
        client.run(200)
    else:
        cCriticalDom("reolink")("Failed to setup ReolinkClient")
