# T1.8 / F5 — rate-limit & ban must not be bypassable via client-controlled
# X-Forwarded-For, and the per-IP state dicts must stay bounded.
#
# Deployment model (docs/refactoring/DECISIONS.md): calaos_server always sits
# behind haproxy in calaos-os. haproxy APPENDS the real client IP as the LAST
# entry of X-Forwarded-For; any earlier entries are attacker-controlled. The
# C++ Unix-socket proxy forwards bytes as-is. So the only trustworthy client
# identity is the last XFF entry.
import pytest

fastapi = pytest.importorskip("fastapi")
pytest.importorskip("httpx")

from fastapi import FastAPI
from fastapi.testclient import TestClient

import calaos_mcp.auth as auth
from calaos_mcp.auth import BearerAuthMiddleware

TOKEN = "sekret-token"
REAL_IP = "192.0.2.10"  # what haproxy appends (trusted, last entry)


def make_client(rate_limit=300, ban_failures=20, ban_seconds=120) -> TestClient:
    app = FastAPI()

    @app.get("/healthz")
    async def healthz():
        return {"ok": True}

    @app.get("/data")
    async def data():
        return {"ok": True}

    app.add_middleware(
        BearerAuthMiddleware,
        expected_token=TOKEN,
        rate_limit=rate_limit,
        ban_failures=ban_failures,
        ban_seconds=ban_seconds,
    )
    return TestClient(app)


@pytest.fixture(autouse=True)
def clean_state():
    auth._reset_state()
    yield
    auth._reset_state()


def hdr(xff=None, token=TOKEN):
    h = {"Authorization": f"Bearer {token}"}
    if xff is not None:
        h["X-Forwarded-For"] = xff
    return h


def test_healthz_needs_no_auth():
    c = make_client()
    assert c.get("/healthz").status_code == 200


def test_valid_token_passes():
    c = make_client()
    assert c.get("/data", headers=hdr(xff=REAL_IP)).status_code == 200


def test_invalid_token_rejected():
    c = make_client()
    assert c.get("/data", headers=hdr(xff=REAL_IP, token="nope")).status_code == 401


def test_ban_not_bypassable_by_rotating_client_xff_prefix():
    """Attacker rotates the XFF prefix; haproxy appends the real IP last.

    The ban key must be the trusted (last) entry, so rotation must NOT
    evade the ban.
    """
    c = make_client(ban_failures=3, ban_seconds=120)
    for i in range(3):
        r = c.get("/data", headers=hdr(xff=f"10.66.{i}.1, {REAL_IP}", token="bad"))
        assert r.status_code == 401
    # Banned now — even with a fresh spoofed prefix
    r = c.get("/data", headers=hdr(xff=f"10.99.99.99, {REAL_IP}", token="bad"))
    assert r.status_code == 429
    # A valid token from the banned client is also throttled
    r = c.get("/data", headers=hdr(xff=f"172.16.0.1, {REAL_IP}"))
    assert r.status_code == 429


def test_rate_limit_not_bypassable_by_rotating_client_xff_prefix():
    c = make_client(rate_limit=5)
    for i in range(5):
        assert c.get("/data", headers=hdr(xff=f"10.0.{i}.1, {REAL_IP}")).status_code == 200
    assert c.get("/data", headers=hdr(xff=f"10.0.77.1, {REAL_IP}")).status_code == 429


def test_distinct_real_clients_get_distinct_buckets():
    c = make_client(rate_limit=3)
    for _ in range(3):
        assert c.get("/data", headers=hdr(xff="192.0.2.1")).status_code == 200
    # first client throttled...
    assert c.get("/data", headers=hdr(xff="192.0.2.1")).status_code == 429
    # ...but a different real client is not
    assert c.get("/data", headers=hdr(xff="192.0.2.2")).status_code == 200


def test_state_dicts_are_bounded():
    """Even with unlimited distinct client IPs, tracked state stays capped."""
    c = make_client(rate_limit=300, ban_failures=1, ban_seconds=9999)
    n = auth.MAX_TRACKED_IPS + 50
    for i in range(n):
        ip = f"198.51.{i // 250}.{i % 250}"
        c.get("/data", headers=hdr(xff=ip, token="bad"))
    assert len(auth._req_counts) <= auth.MAX_TRACKED_IPS
    assert len(auth._fail_counts) <= auth.MAX_TRACKED_IPS
    assert len(auth._ban_until) <= auth.MAX_TRACKED_IPS


def test_throttle_keys_on_last_xff_header_line_not_first():
    """REAL haproxy threat model: `option forwardfor` APPENDS A NEW
    X-Forwarded-For HEADER LINE after any client-supplied ones — it does not
    merge. headers.get() would return the first (attacker-controlled) line;
    the throttle must key on the LAST line instead.
    """
    c = make_client(rate_limit=3)

    def dup_hdr(spoof):
        # duplicate header lines: rotating attacker line first, fixed
        # haproxy-appended line last
        return [("Authorization", f"Bearer {TOKEN}"),
                ("X-Forwarded-For", spoof),
                ("X-Forwarded-For", REAL_IP)]

    for i in range(3):
        assert c.get("/data", headers=dup_hdr(f"10.44.{i}.1")).status_code == 200
    # 4th request with a fresh spoofed first line must still trip the limit
    assert c.get("/data", headers=dup_hdr("10.99.99.99")).status_code == 429
    # State must be keyed only on the trusted IP, not the spoofed ones
    assert REAL_IP in auth._req_counts
    assert not any(ip.startswith("10.") for ip in auth._req_counts)


def test_ban_keys_on_last_xff_header_line():
    """Same duplicate-header threat model, applied to the ban list."""
    c = make_client(ban_failures=3, ban_seconds=120)

    def dup_hdr(spoof, token):
        return [("Authorization", f"Bearer {token}"),
                ("X-Forwarded-For", spoof),
                ("X-Forwarded-For", REAL_IP)]

    for i in range(3):
        assert c.get("/data", headers=dup_hdr(f"10.55.{i}.1", "bad")).status_code == 401
    # Banned now — rotating the first header line must not evade it
    assert c.get("/data", headers=dup_hdr("10.77.0.1", "bad")).status_code == 429
    assert c.get("/data", headers=dup_hdr("10.88.0.1", TOKEN)).status_code == 429
    assert set(auth._ban_until) == {REAL_IP}


def test_rate_limit_zero_disables_throttling():
    """rate_limit <= 0 means disabled (config.py normalizes it upstream)."""
    c = make_client(rate_limit=0)
    for _ in range(20):
        assert c.get("/data", headers=hdr(xff=REAL_IP)).status_code == 200


def test_success_resets_failure_counter():
    c = make_client(ban_failures=3)
    for _ in range(2):
        assert c.get("/data", headers=hdr(xff=REAL_IP, token="bad")).status_code == 401
    assert c.get("/data", headers=hdr(xff=REAL_IP)).status_code == 200
    # counter was reset: two more failures do not ban
    for _ in range(2):
        assert c.get("/data", headers=hdr(xff=REAL_IP, token="bad")).status_code == 401
    assert c.get("/data", headers=hdr(xff=REAL_IP)).status_code == 200
