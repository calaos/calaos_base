"""Entry point for the calaos_mcp sidecar.

Listens on a Unix domain socket whose path is given via the
CALAOS_MCP_SOCKET environment variable, and exposes a minimal FastAPI app.
At this stage only the /healthz endpoint is implemented; MCP routes will
be added in a later phase.

The socket is pre-bound here with explicit 0660 permissions and handed to
uvicorn via the file descriptor parameter. This bypasses uvicorn's default
post-bind chmod to 0o666 — see security mitigation S6 in the plan.
"""

from __future__ import annotations

import logging
import os
import socket
import sys

import uvicorn

from calaos_mcp.server import create_app

LOG = logging.getLogger("calaos_mcp")


def _resolve_socket_path() -> str:
    path = os.environ.get("CALAOS_MCP_SOCKET")
    if not path:
        sys.stderr.write(
            "calaos_mcp: CALAOS_MCP_SOCKET environment variable is required\n"
        )
        sys.exit(2)
    return path


def _calaos_log_level() -> str:
    """Translate the calaos numeric debug_level convention into a uvicorn
    log level name. Calaos uses 1..5 (CRITICAL..DEBUG). Anything outside
    that range, including the string "info", "debug" etc., is passed
    through unchanged so the user can also set the env var to a uvicorn
    name directly.
    """
    raw = os.environ.get("CALAOS_LOG_LEVEL", "info").strip()
    if raw == "":
        return "info"
    if raw.isdigit():
        return {
            "1": "critical",
            "2": "error",
            "3": "warning",
            "4": "info",
            "5": "debug",
        }.get(raw, "info")
    return raw.lower()


def _configure_logging() -> None:
    name = _calaos_log_level().upper()
    level = getattr(logging, name, logging.INFO)
    logging.basicConfig(
        level=level,
        format="%(asctime)s [mcp] %(levelname)s %(name)s: %(message)s",
    )


def _bind_socket(path: str) -> socket.socket:
    # Remove any leftover socket file from a previous, possibly crashed,
    # instance. McpServerManager on the C++ side also unlinks before spawn.
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass

    # S6: bind with umask 0o117 so the inode is created 0o660 (no access for
    # other users), then chmod explicitly to lock down even if umask was
    # overridden somewhere up the call stack.
    previous_umask = os.umask(0o117)
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.bind(path)
    finally:
        os.umask(previous_umask)

    os.chmod(path, 0o660)
    sock.listen(128)
    return sock


def main() -> None:
    _configure_logging()
    socket_path = _resolve_socket_path()
    sock = _bind_socket(socket_path)

    LOG.info("calaos_mcp sidecar listening on uds=%s (fd=%d)",
             socket_path, sock.fileno())

    config = uvicorn.Config(
        create_app(),
        fd=sock.fileno(),
        log_level=_calaos_log_level(),
        access_log=False,
    )
    server = uvicorn.Server(config)
    try:
        server.run()
    finally:
        try:
            os.unlink(socket_path)
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    main()
