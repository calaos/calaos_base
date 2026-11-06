# T1.8 / F5 — rate-limit & ban must not be bypassable by a client that writes
# its own identity, and the per-IP state dicts must stay bounded.
#
# Deployment model (docs/refactoring/DECISIONS.md, docs/15_mcp_server.md): the
# sidecar listens on a Unix socket, so request.client is None and no header the
# client can write says anything about where it came from. The C++ /mcp relay
# is the only place that sees the TCP peer; it strips the client's own
# X-Calaos-Client lines from every request head and appends one of its own,
# carrying a credential derived from mcp_service_token. That header, and
# nothing else, is the identity here.
import pytest

fastapi = pytest.importorskip("fastapi")
pytest.importorskip("httpx")

from fastapi import FastAPI
from fastapi.testclient import TestClient

import calaos_mcp.auth as auth
from calaos_mcp.auth import BearerAuthMiddleware, UNIDENTIFIED

TOKEN = "sekret-token"
CRED = "c0ffee" * 10          # stands in for the derived relay credential
REAL_IP = "192.0.2.10"        # what the relay measured as the TCP peer


def make_client(rate_limit=300, ban_failures=20, ban_seconds=120,
                proxy_credential=CRED) -> TestClient:
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
        proxy_credential=proxy_credential,
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


def relayed(ip, token=TOKEN, cred=CRED):
    """Headers as the C++ relay writes them."""
    return {"Authorization": f"Bearer {token}",
            "X-Calaos-Client": f"{cred} {ip}"}


def test_healthz_needs_no_auth():
    c = make_client()
    assert c.get("/healthz").status_code == 200


def test_valid_token_passes():
    c = make_client()
    assert c.get("/data", headers=relayed(REAL_IP)).status_code == 200


def test_invalid_token_rejected():
    c = make_client()
    assert c.get("/data", headers=relayed(REAL_IP, token="nope")).status_code == 401


# --- F-MCP-XFF-1 -------------------------------------------------------------
# The two cases below rotate X-Forwarded-For on every request. That rotation is
# the whole point: a fixed key trips the limiter no matter what the middleware
# trusts, so a case that does not rotate proves nothing.
#
# The sidecar is reached over a Unix socket, so request.client is None and no
# peer check like the C++ one (TransportLimits::isTrustedProxyPeer) can be
# written here: whatever the sidecar believes has to be written by the relay.


def test_rotating_forwarded_for_cannot_escape_the_rate_limit():
    c = make_client(rate_limit=5)
    codes = [
        c.get("/data", headers=hdr(xff=f"10.0.{i}.7")).status_code
        for i in range(20)
    ]
    assert codes.count(200) == 5
    assert codes.count(429) == 15


def test_rotating_forwarded_for_cannot_ban_someone_elses_bucket():
    c = make_client(ban_failures=3, ban_seconds=120)
    for i in range(3):
        assert c.get("/data", headers=hdr(xff=f"10.1.{i}.7", token="bad")).status_code == 401
    # A fourth failure under yet another forged address must still be the same
    # bucket, hence banned.
    assert c.get("/data", headers=hdr(xff="10.1.99.7", token="bad")).status_code == 429
    assert list(auth._ban_until) != []
    assert not any(ip.startswith("10.1.") for ip in auth._ban_until)


def test_forwarded_for_is_not_an_identity():
    c = make_client(rate_limit=3)
    for _ in range(3):
        assert c.get("/data", headers=hdr(xff="203.0.113.9")).status_code == 200
    assert list(auth._req_counts) == [UNIDENTIFIED]


def test_a_forged_credential_buys_nothing():
    """A client can write X-Calaos-Client too; it just does not check out."""
    c = make_client(rate_limit=5)
    codes = [
        c.get("/data",
              headers=relayed(f"10.2.{i}.7", cred="f" * 60)).status_code
        for i in range(20)
    ]
    assert codes.count(200) == 5
    assert codes.count(429) == 15
    assert list(auth._req_counts) == [UNIDENTIFIED]


def test_a_second_trusted_line_is_refused():
    """The relay writes exactly one line, so two means one is the client's."""
    c = make_client(rate_limit=3)
    headers = [("Authorization", f"Bearer {TOKEN}"),
               ("X-Calaos-Client", f"{CRED} 198.51.100.4"),
               ("X-Calaos-Client", f"{CRED} 198.51.100.5")]
    assert c.get("/data", headers=headers).status_code == 200
    assert list(auth._req_counts) == [UNIDENTIFIED]


def test_distinct_relayed_clients_get_distinct_buckets():
    """The witness: behind the relay, two real clients keep two buckets."""
    c = make_client(rate_limit=3)
    for _ in range(3):
        assert c.get("/data", headers=relayed("192.0.2.1")).status_code == 200
    assert c.get("/data", headers=relayed("192.0.2.1")).status_code == 429
    assert c.get("/data", headers=relayed("192.0.2.2")).status_code == 200


def test_ban_keys_on_the_relayed_identity():
    c = make_client(ban_failures=3, ban_seconds=120)
    for _ in range(3):
        assert c.get("/data", headers=relayed(REAL_IP, token="bad")).status_code == 401
    assert c.get("/data", headers=relayed(REAL_IP, token="bad")).status_code == 429
    assert c.get("/data", headers=relayed(REAL_IP)).status_code == 429
    assert set(auth._ban_until) == {REAL_IP}
    # ...and a different real client is untouched by that ban
    assert c.get("/data", headers=relayed("192.0.2.99")).status_code == 200


def test_no_credential_configured_trusts_nothing():
    c = make_client(rate_limit=3, proxy_credential="")
    for _ in range(3):
        assert c.get("/data", headers=relayed(REAL_IP)).status_code == 200
    assert c.get("/data", headers=relayed("192.0.2.77")).status_code == 429
    assert list(auth._req_counts) == [UNIDENTIFIED]


def test_state_dicts_are_bounded():
    """Even with unlimited distinct client IPs, tracked state stays capped."""
    c = make_client(rate_limit=300, ban_failures=1, ban_seconds=9999)
    n = auth.MAX_TRACKED_IPS + 50
    for i in range(n):
        ip = f"198.51.{i // 250}.{i % 250}"
        c.get("/data", headers=relayed(ip, token="bad"))
    assert len(auth._req_counts) <= auth.MAX_TRACKED_IPS
    assert len(auth._fail_counts) <= auth.MAX_TRACKED_IPS
    assert len(auth._ban_until) <= auth.MAX_TRACKED_IPS


def test_rate_limit_zero_disables_throttling():
    """rate_limit <= 0 means disabled (config.py normalizes it upstream)."""
    c = make_client(rate_limit=0)
    for _ in range(20):
        assert c.get("/data", headers=relayed(REAL_IP)).status_code == 200


def test_success_resets_failure_counter():
    c = make_client(ban_failures=3)
    for _ in range(2):
        assert c.get("/data", headers=relayed(REAL_IP, token="bad")).status_code == 401
    assert c.get("/data", headers=relayed(REAL_IP)).status_code == 200
    # counter was reset: two more failures do not ban
    for _ in range(2):
        assert c.get("/data", headers=relayed(REAL_IP, token="bad")).status_code == 401
    assert c.get("/data", headers=relayed(REAL_IP)).status_code == 200
