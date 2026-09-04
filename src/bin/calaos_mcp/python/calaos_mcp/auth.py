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

Client identity (F5): we listen on a Unix socket, so `request.client` is None
and no request carries a peer we could test. X-Forwarded-For is therefore
worthless here — port 5454 answers the LAN directly, and such a client writes
that header itself, choosing its own bucket or a victim's.

The identity comes from X-Calaos-Client instead, written by the C++ /mcp relay
on every request head after stripping the client's own: it is the only place
where the TCP peer is known. Its first field is a secret derived from
mcp_service_token, which no MCP client ever sees, so a forged line is refused
rather than believed. Requests without a valid one share a single bucket.

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

# Written by the C++ /mcp relay: "<credential> <client ip>".
TRUSTED_HEADER = "x-calaos-client"
# Bucket shared by everything that did not come through the relay.
UNIDENTIFIED = "unknown"

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


def _source_ip(request: Request, credential: str) -> str:
    # The relay drops every client-written line of this header and appends
    # exactly one of its own, so a request carrying any other number of them
    # did not come through it.
    lines = request.headers.getlist(TRUSTED_HEADER)
    if credential and len(lines) == 1:
        received, _, ip = lines[0].partition(" ")
        if ip and hmac.compare_digest(received, credential):
            return ip
    return UNIDENTIFIED


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
                 proxy_credential: str = "",
                 rate_limit: int = 300,
                 ban_failures: int = 20,
                 ban_seconds: int = 120):
        super().__init__(app)
        self._expected = expected_token
        self._credential = proxy_credential
        self._rate_limit = rate_limit
        self._ban_failures = ban_failures
        self._ban_seconds = ban_seconds

    async def dispatch(self, request: Request, call_next):
        # Always allow healthz without auth
        if request.url.path in ("/healthz", "/mcp/healthz"):
            return await call_next(request)

        ip = _source_ip(request, self._credential)
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
