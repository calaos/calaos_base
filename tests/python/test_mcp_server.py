# The sidecar app end to end, minus calaos_server: the MCP handshake must
# succeed under the Host header the C++ relay forwards. mcp 2 rejects any
# non-localhost Host with 421 unless server.py passes its transport settings
# explicitly, and nothing short of a real request notices.
import json

import pytest

pytest.importorskip("fastapi")
pytest.importorskip("httpx")
pytest.importorskip("mcp.server.mcpserver")

from fastapi.testclient import TestClient  # noqa: E402

TOKEN = "a" * 64
PROTOCOL_VERSION = "2025-06-18"
EXPECTED_TOOLS = {
    "list_rooms", "get_room", "list_ios", "find_io", "get_io_state",
    "set_io_state", "list_scenarios", "run_scenario", "audio_control",
}


@pytest.fixture
def client(tmp_path, monkeypatch):
    (tmp_path / "local_config.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<calaos:config xmlns:calaos="http://www.calaos.fr">\n'
        f'  <calaos:option name="mcp_token" value="{TOKEN}"/>\n'
        '  <calaos:option name="mcp_service_token" value="service"/>\n'
        '</calaos:config>\n')
    monkeypatch.setenv("CALAOS_CONFIG_PATH", str(tmp_path))
    monkeypatch.setenv("CALAOS_MCP_SOCKET", str(tmp_path / "mcp.sock"))

    from calaos_mcp import auth, config, server

    async def no_connect(self):
        pass

    # No calaos_server to talk to: the reconnect loop would outlive the test.
    monkeypatch.setattr(server.CalaosClient, "connect", no_connect)
    config.get_config.cache_clear()
    auth._reset_state()
    try:
        with TestClient(server.create_app(),
                        base_url="http://calaos.local") as c:
            yield c
    finally:
        config.get_config.cache_clear()
        auth._reset_state()


def rpc(client, method, params=None, msg_id=1, session=None):
    headers = {
        "Authorization": f"Bearer {TOKEN}",
        "Accept": "application/json, text/event-stream",
        "Content-Type": "application/json",
    }
    if session:
        headers["Mcp-Session-Id"] = session
        headers["Mcp-Protocol-Version"] = PROTOCOL_VERSION
    body = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if msg_id is not None:
        body["id"] = msg_id
    return client.post("/mcp", headers=headers, content=json.dumps(body))


def result_of(resp):
    if resp.headers.get("content-type", "").startswith("application/json"):
        return resp.json()["result"]
    for line in resp.text.splitlines():
        if line.startswith("data:"):
            return json.loads(line[len("data:"):])["result"]
    raise AssertionError(f"no JSON-RPC result in {resp.text!r}")


def initialize(client):
    resp = rpc(client, "initialize", {
        "protocolVersion": PROTOCOL_VERSION,
        "capabilities": {},
        "clientInfo": {"name": "pytest", "version": "0"},
    })
    assert resp.status_code == 200, (resp.status_code, resp.text)
    return resp


def test_initialize_accepts_the_relayed_host(client):
    resp = initialize(client)
    assert result_of(resp)["serverInfo"]["name"] == "calaos"


def test_tools_list_exposes_every_tool(client):
    resp = initialize(client)
    session = resp.headers.get("mcp-session-id")
    assert session, "stateful transport must hand out a session id"
    notif = rpc(client, "notifications/initialized", msg_id=None,
                session=session)
    assert notif.status_code == 202, (notif.status_code, notif.text)

    resp = rpc(client, "tools/list", msg_id=2, session=session)
    assert resp.status_code == 200, (resp.status_code, resp.text)
    names = {t["name"] for t in result_of(resp)["tools"]}
    assert names == EXPECTED_TOOLS
