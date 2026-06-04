"""Bearer token authentication middleware (S7, S11).

Validates Authorization: Bearer <token> using constant-time comparison.
Rate-limits per source IP: 30 req/min, ban 10 min after 5 consecutive
auth failures.
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

RATE_LIMIT = 30       # requests per minute
BURST = 10
BAN_FAILURES = 5
BAN_SECONDS = 600


def _source_ip(request: Request) -> str:
    forwarded = request.headers.get("x-forwarded-for", "")
    if forwarded:
        return forwarded.split(",")[0].strip()
    return request.client.host if request.client else "unknown"


class BearerAuthMiddleware(BaseHTTPMiddleware):
    def __init__(self, app, expected_token: str):
        super().__init__(app)
        self._expected = expected_token

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
            if len(window) >= RATE_LIMIT:
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
            if _fail_counts[ip] >= BAN_FAILURES:
                _ban_until[ip] = time.monotonic() + BAN_SECONDS
                _fail_counts[ip] = 0
                LOG.warning("IP %s banned for %ds after %d failures",
                             ip, BAN_SECONDS, BAN_FAILURES)
        return Response("Unauthorized", status_code=401,
                        headers={"WWW-Authenticate": "Bearer"})
