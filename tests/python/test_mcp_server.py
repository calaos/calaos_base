# The sidecar app end to end, minus calaos_server. Each case guards something
# mcp 2 changed under server.py without an import failing: any non-localhost
# Host gets 421 unless the transport settings are explicit, and a tool's error
# text is withheld from the model unless it is raised as ToolError.
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


def open_session(client):
    resp = initialize(client)
    session = resp.headers.get("mcp-session-id")
    assert session, "stateful transport must hand out a session id"
    notif = rpc(client, "notifications/initialized", msg_id=None,
                session=session)
    assert notif.status_code == 202, (notif.status_code, notif.text)
    return session


def call_tool(client, name, arguments):
    session = open_session(client)
    resp = rpc(client, "tools/call", {"name": name, "arguments": arguments},
               msg_id=2, session=session)
    assert resp.status_code == 200, (resp.status_code, resp.text)
    result = result_of(resp)
    return result.get("isError"), result["content"][0]["text"]


def test_initialize_accepts_the_relayed_host(client):
    from calaos_mcp import __version__

    info = result_of(initialize(client))["serverInfo"]
    assert info["name"] == "calaos"
    assert info["version"] == __version__


def test_tools_list_exposes_every_tool(client):
    session = open_session(client)
    resp = rpc(client, "tools/list", msg_id=2, session=session)
    assert resp.status_code == 200, (resp.status_code, resp.text)
    names = {t["name"] for t in result_of(resp)["tools"]}
    assert names == EXPECTED_TOOLS


def test_mcp_requires_the_bearer_token(client):
    resp = client.post("/mcp", headers={
        "Accept": "application/json, text/event-stream",
        "Content-Type": "application/json",
    }, content="{}")
    assert resp.status_code == 401


def test_healthz_needs_no_token(client):
    for path in ("/healthz", "/mcp/healthz"):
        resp = client.get(path)
        assert resp.status_code == 200, (path, resp.status_code)
        assert resp.json()["status"] == "ok"


def test_unknown_room_error_reaches_the_model(client, monkeypatch):
    from calaos_mcp import server

    async def get_home(self, force=False):
        return {"home": [{"name": "Salon", "items": []}]}

    monkeypatch.setattr(server.CalaosClient, "get_home", get_home)
    is_error, text = call_tool(client, "get_room", {"room_name": "Nope"})
    assert is_error
    assert "Room 'Nope' not found. Known rooms: ['Salon']" in text


def test_not_connected_error_reaches_the_model(client):
    is_error, text = call_tool(client, "list_rooms", {})
    assert is_error
    assert "Not connected to calaos_server yet" in text
