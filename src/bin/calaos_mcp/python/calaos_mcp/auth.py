"""Bearer token authentication middleware (S7, S11).

Validates Authorization: Bearer <token> using constant-time comparison.
Rate-limits per source IP. The limits are read from local_config.xml (see
calaos_mcp.config) so operators can loosen them for trusted clients — e.g. an
automation agent that bursts many tool calls:

    mcp_rate_limit    requests per minute per IP            (default 300)
    mcp_ban_failures  consecutive auth failures before ban  (default 20,
                      set 0 to disable banning entirely)
    mcp_ban_seconds   ban duration in seconds               (default 120)
"""

from __future__ import annotations

import hmac
import logging
import time
from collections import defaultdict
from threading import Lock

from fastapi import Request, Response
from starlette.middleware.base import BaseHTTPMiddleware

LOG = logging.getLogger("calaos_mcp.auth")

# Per-IP rate-limit state
_lock = Lock()
_req_counts: dict[str, list[float]] = defaultdict(list)   # sliding window (60s)
_fail_counts: dict[str, int] = defaultdict(int)
_ban_until: dict[str, float] = {}


def _source_ip(request: Request) -> str:
    forwarded = request.headers.get("x-forwarded-for", "")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.client.host if request.client else "unknown"


class BearerAuthMiddleware(BaseHTTPMiddleware):
    def __init__(self, app, expected_token: str,
                 rate_limit: int = 300,
                 ban_failures: int = 20,
                 ban_seconds: int = 120):
        super().__init__(app)
        self._expected = expected_token
        self._rate_limit = rate_limit
        self._ban_failures = ban_failures
        self._ban_seconds = ban_seconds

    async def dispatch(self, request: Request, call_next):
        # Always allow healthz without auth
        if request.url.path in ("/healthz", "/mcp/healthz"):
            return await call_next(request)

        ip = _source_ip(request)
        now = time.monotonic()

        with _lock:
            # Check ban
            if _ban_until.get(ip, 0) > now:
                remaining = int(_ban_until[ip] - now)
                LOG.warning("IP %s is banned (%ds remaining)", ip, remaining)
                return Response("Too Many Requests", status_code=429)

            # Sliding-window rate limit
            window = [t for t in _req_counts[ip] if now - t < 60]
            if len(window) >= self._rate_limit:
                LOG.warning("IP %s rate-limited", ip)
                return Response("Too Many Requests", status_code=429)
            window.append(now)
            _req_counts[ip] = window

        # Validate Bearer token (S7: constant-time)
        auth = request.headers.get("authorization", "")
        if not auth.lower().startswith("bearer "):
            return await self._auth_fail(ip, "Missing Bearer token")

        received = auth[7:].strip()
        if not hmac.compare_digest(received.encode(), self._expected.encode()):
            return await self._auth_fail(ip, "Invalid Bearer token")

        # Auth success — reset failure counter
        with _lock:
            _fail_counts[ip] = 0

        return await call_next(request)

    async def _auth_fail(self, ip: str, reason: str) -> Response:
        LOG.warning("Auth failure from %s: %s", ip, reason)
        with _lock:
            _fail_counts[ip] += 1
            if self._ban_failures > 0 and _fail_counts[ip] >= self._ban_failures:
                _ban_until[ip] = time.monotonic() + self._ban_seconds
                _fail_counts[ip] = 0
                LOG.warning("IP %s banned for %ds after %d failures",
                             ip, self._ban_seconds, self._ban_failures)
        return Response("Unauthorized", status_code=401,
                        headers={"WWW-Authenticate": "Bearer"})
