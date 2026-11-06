# McpServerManager files each sidecar line by its leading level name, because
# Python logging writes every level to stderr. These cases pin the sidecar side.
import logging

import pytest

pytest.importorskip("uvicorn")

from calaos_mcp import __main__ as sidecar  # noqa: E402


@pytest.fixture
def sidecar_formatter(monkeypatch):
    # pytest keeps handlers on the root logger, which turns basicConfig into a
    # no-op, so read back what the sidecar asks for.
    seen = {}
    monkeypatch.setattr(logging, "basicConfig", lambda **kw: seen.update(kw))
    monkeypatch.setenv("CALAOS_LOG_LEVEL", "debug")
    sidecar._configure_logging()
    return logging.Formatter(seen["format"])


def _record(level, msg="hello", exc_info=None):
    return logging.LogRecord("calaos_mcp.server", level, __file__, 1, msg,
                             None, exc_info)


@pytest.mark.parametrize("level,name", [
    (logging.DEBUG, "DEBUG"), (logging.INFO, "INFO"),
    (logging.WARNING, "WARNING"), (logging.ERROR, "ERROR"),
    (logging.CRITICAL, "CRITICAL"),
])
def test_line_starts_with_level_name(sidecar_formatter, level, name):
    out = sidecar_formatter.format(_record(level))
    assert out.startswith(name + " calaos_mcp.server: hello")


def test_traceback_follows_its_error_line(sidecar_formatter):
    try:
        raise ValueError("boom")
    except ValueError:
        import sys
        rec = _record(logging.ERROR, "failed", sys.exc_info())
    lines = sidecar_formatter.format(rec).splitlines()
    assert lines[0].startswith("ERROR ")
    assert any(l.startswith("Traceback") for l in lines[1:])


def test_uvicorn_does_not_install_its_own_stderr_format(monkeypatch, tmp_path):
    seen = {}

    class FakeServer:
        def __init__(self, config):
            pass

        def run(self):
            pass

    def fake_config(app, **kw):
        seen.update(kw)
        return object()

    monkeypatch.setenv("CALAOS_MCP_SOCKET", str(tmp_path / "s.sock"))
    monkeypatch.setattr(sidecar.uvicorn, "Config", fake_config)
    monkeypatch.setattr(sidecar.uvicorn, "Server", FakeServer)
    monkeypatch.setattr(sidecar, "create_app", lambda: object())
    monkeypatch.setattr(sidecar, "_configure_logging", lambda: None)
    monkeypatch.setattr(sidecar.sys, "argv", ["calaos_mcp"])
    sidecar.main()
    assert "log_config" in seen and seen["log_config"] is None
