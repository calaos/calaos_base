"""Persistent WebSocket client to calaos_server JsonApi.

Authenticates via login_service (S2 — service-scoped account, not admin
credentials). Provides async wrappers for the MCP tools. Reconnects
automatically with exponential backoff.
"""

from __future__ import annotations

import asyncio
import json
import logging
import time
from typing import Any

import websockets

from calaos_mcp.config import get_config

LOG = logging.getLogger("calaos_mcp.client")

# Actions permitted under the service scope (S2).
_ALLOWED_ACTIONS = frozenset({
    "get_home", "get_state", "get_states", "query",
    "get_param", "set_state", "get_io", "audio", "autoscenario",
    "get_playlist", "get_timerange", "get_camera_pic",
})


class CalaosClient:
    def __init__(self) -> None:
        self._ws: websockets.WebSocketClientProtocol | None = None
        self._ready = asyncio.Event()
        self._pending: dict[str, asyncio.Future] = {}
        self._msg_counter = 0
        self._home_cache: dict | None = None
        self._task: asyncio.Task | None = None

    async def connect(self) -> None:
        """Start the background reconnect loop. Does not block until connected.

        The WS connection is established asynchronously; tool calls before the
        first successful connect will raise RuntimeError("not connected yet").
        """
        self._task = asyncio.create_task(self._run_loop())

    async def _run_loop(self) -> None:
        delay = 1.0
        while True:
            try:
                await self._connect_once()
                delay = 1.0
            except Exception as exc:
                LOG.warning("WebSocket disconnected: %s — retry in %.0fs", exc, delay)
                self._ready.clear()
                await asyncio.sleep(delay)
                delay = min(delay * 2, 60.0)

    async def _connect_once(self) -> None:
        cfg = get_config()
        LOG.info("Connecting to %s", cfg.api_url)
        async with websockets.connect(cfg.api_url) as ws:
            self._ws = ws
            # Authenticate as service account (S2)
            msg_id = self._next_id()
            await ws.send(json.dumps({
                "msg": "login_service",
                "msg_id": msg_id,
                "data": {"token": cfg.service_token},
            }))
            resp = json.loads(await ws.recv())
            if resp.get("data", {}).get("success") != "true":
                raise RuntimeError(f"login_service failed: {resp}")
            LOG.info("Authenticated to calaos_server (service scope)")
            self._ready.set()

            async for raw in ws:
                msg = json.loads(raw)
                msg_type = msg.get("msg", "")
                msg_id = msg.get("msg_id", "")

                # Resolve pending request futures
                if msg_id and msg_id in self._pending:
                    fut = self._pending.pop(msg_id)
                    if not fut.done():
                        fut.set_result(msg.get("data", {}))
                    continue

                # Invalidate home cache on topology events
                if msg_type in ("io_added", "io_deleted", "room_added", "room_deleted"):
                    self._home_cache = None

    def _next_id(self) -> str:
        self._msg_counter += 1
        return f"mcp-{self._msg_counter}"

    async def _request(self, msg_type: str, data: dict | None = None) -> dict:
        if not self._ws or not self._ready.is_set():
            raise RuntimeError(
                "Not connected to calaos_server yet. "
                "The server may be starting up — retry in a moment."
            )
        msg_id = self._next_id()
        fut: asyncio.Future = asyncio.get_event_loop().create_future()
        self._pending[msg_id] = fut
        await self._ws.send(json.dumps({
            "msg": msg_type,
            "msg_id": msg_id,
            "data": data or {},
        }))
        return await asyncio.wait_for(fut, timeout=10.0)

    async def get_home(self, force: bool = False) -> dict:
        if self._home_cache is None or force:
            self._home_cache = await self._request("get_home")
        return self._home_cache

    async def get_state(self, io_ids: list[str]) -> dict:
        # get_state expects a flat array of IO id strings and returns a flat
        # mapping {io_id: state_value}.
        return await self._request("get_state", {"items": list(io_ids)})

    async def set_state(self, io_id: str, value: str) -> dict:
        return await self._request("set_state", {"id": io_id, "value": value})

    async def audio(self, player_id: str, action: str, **kwargs) -> dict:
        data: dict[str, Any] = {"player_id": player_id, "action": action, **kwargs}
        return await self._request("audio", data)

    async def autoscenario(self, scenario_type: str, **kwargs) -> dict:
        data: dict[str, Any] = {"type": scenario_type, **kwargs}
        return await self._request("autoscenario", data)
