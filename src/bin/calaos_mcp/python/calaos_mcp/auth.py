"""Bearer token authentication middleware (S7, S11).

Validates Authorization: Bearer <token> using constant-time comparison.
Rate-limits per source IP. The limits are read from local_config.xml (see
calaos_mcp.config) so operators can loosen them for trusted clients — e.g. an
automation agent that bursts many tool calls:

    mcp_rate_limit    requests per minute per IP            (default 300,
                      <= 0 disables rate limiting entirely)
    mcp_ban_failures  consecutive auth failures before ban  (default 20,
                      set 0 to disable banning entirely)
    mcp_ban_seconds   ban duration in seconds               (default 120)

Client identity (F5): in calaos-os, calaos_server always sits behind haproxy,
and the C++ Unix-socket proxy forwards the request bytes as-is. haproxy
APPENDS the real client IP as the LAST entry of X-Forwarded-For; any earlier
entries are attacker-controlled and must not be trusted. We therefore key the
rate-limiter/ban-list on the LAST X-Forwarded-For entry (the trusted proxy
hop), never the first. Without the header, all requests share one bucket
(direct Unix-socket access has no per-client identity to offer anyway).

The per-IP state is bounded (MAX_TRACKED_IPS) and periodically pruned so a
client rotating addresses cannot grow memory without limit.
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

# Sliding-window length for the rate limiter (seconds)
_WINDOW_SECONDS = 60.0
# Hard cap on tracked client entries per state dict (memory bound)
MAX_TRACKED_IPS = 4096
# How often (seconds) stale entries are garbage-collected
_PRUNE_INTERVAL = 60.0

# Per-IP rate-limit state
_lock = Lock()
_req_counts: dict[str, list[float]] = defaultdict(list)   # sliding window
_fail_counts: dict[str, int] = defaultdict(int)
_ban_until: dict[str, float] = {}
_last_prune = 0.0


def _reset_state() -> None:
    """Clear all throttling state (used by tests)."""
    global _last_prune
    with _lock:
        _req_counts.clear()
        _fail_counts.clear()
        _ban_until.clear()
        _last_prune = 0.0


def _source_ip(request: Request) -> str:
    forwarded = request.headers.get("x-forwarded-for", "")
    if forwarded:
        # Trust only the LAST entry: it is appended by the nearest trusted
        # proxy hop (haproxy in calaos-os). Earlier entries are supplied by
        # the client and can be rotated per request to evade throttling.
        last = forwarded.rsplit(",", 1)[-1].strip()
        if last:
            return last
    return request.client.host if request.client else "unknown"


def _prune_locked(now: float) -> None:
    """Drop stale entries and enforce MAX_TRACKED_IPS. Caller holds _lock."""
    # Expired bans
    for ip in [ip for ip, until in _ban_until.items() if until <= now]:
        del _ban_until[ip]
    # Empty / fully-expired request windows
    for ip in list(_req_counts):
        window = [t for t in _req_counts[ip] if now - t < _WINDOW_SECONDS]
        if window:
            _req_counts[ip] = window
        else:
            del _req_counts[ip]
    # Zeroed failure counters
    for ip in [ip for ip, cnt in _fail_counts.items() if cnt <= 0]:
        del _fail_counts[ip]
    # Hard caps: evict least-recently-active / soonest-expiring entries first
    if len(_req_counts) > MAX_TRACKED_IPS:
        by_last_seen = sorted(_req_counts, key=lambda ip: _req_counts[ip][-1])
        for ip in by_last_seen[: len(_req_counts) - MAX_TRACKED_IPS]:
            del _req_counts[ip]
    if len(_fail_counts) > MAX_TRACKED_IPS:
        for ip in list(_fail_counts)[: len(_fail_counts) - MAX_TRACKED_IPS]:
            del _fail_counts[ip]
    if len(_ban_until) > MAX_TRACKED_IPS:
        soonest = sorted(_ban_until, key=_ban_until.get)
        for ip in soonest[: len(_ban_until) - MAX_TRACKED_IPS]:
            del _ban_until[ip]


def _maybe_prune_locked(now: float) -> None:
    """Prune when the interval elapsed or any dict crossed the cap."""
    global _last_prune
    if (now - _last_prune >= _PRUNE_INTERVAL
            or len(_req_counts) > MAX_TRACKED_IPS
            or len(_fail_counts) > MAX_TRACKED_IPS
            or len(_ban_until) > MAX_TRACKED_IPS):
        _prune_locked(now)
        _last_prune = now


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
            _maybe_prune_locked(now)

            # Check ban
            if _ban_until.get(ip, 0) > now:
                remaining = int(_ban_until[ip] - now)
                LOG.warning("IP %s is banned (%ds remaining)", ip, remaining)
                return Response("Too Many Requests", status_code=429)

            # Sliding-window rate limit (<= 0 means disabled; the large
            # RATE_LIMIT_DISABLED sentinel from config.py also passes)
            window = [t for t in _req_counts[ip] if now - t < _WINDOW_SECONDS]
            if self._rate_limit > 0 and len(window) >= self._rate_limit:
                _req_counts[ip] = window
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
            _fail_counts.pop(ip, None)

        return await call_next(request)

    async def _auth_fail(self, ip: str, reason: str) -> Response:
        LOG.warning("Auth failure from %s: %s", ip, reason)
        with _lock:
            _fail_counts[ip] += 1
            if self._ban_failures > 0 and _fail_counts[ip] >= self._ban_failures:
                _ban_until[ip] = time.monotonic() + self._ban_seconds
                del _fail_counts[ip]
                LOG.warning("IP %s banned for %ds after %d failures",
                            ip, self._ban_seconds, self._ban_failures)
            _maybe_prune_locked(time.monotonic())
        return Response("Unauthorized", status_code=401,
                        headers={"WWW-Authenticate": "Bearer"})
