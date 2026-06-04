"""Configuration for the calaos_mcp sidecar.

Reads secrets from local_config.xml (S1 — never from environment variables)
and connection params from environment variables set by McpServerManager.
"""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET
from functools import lru_cache
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    mcp_token: str
    service_token: str
    socket_path: str
    api_url: str
    log_level: str


@lru_cache(maxsize=1)
def get_config() -> Config:
    config_path = os.environ.get("CALAOS_CONFIG_PATH", "")
    socket_path = os.environ.get("CALAOS_MCP_SOCKET", "")
    api_url = os.environ.get("CALAOS_API_URL", "ws://127.0.0.1:5454/api")
    log_level = os.environ.get("CALAOS_LOG_LEVEL", "info")

    if not socket_path:
        raise RuntimeError("CALAOS_MCP_SOCKET environment variable is required")

    # Read tokens from local_config.xml (S1: secrets never in env).
    mcp_token = ""
    service_token = ""
    if config_path:
        xml_path = os.path.join(config_path, "local_config.xml")
        try:
            tree = ET.parse(xml_path)
            root = tree.getroot()
            # local_config.xml uses the calaos namespace, so elements are
            # tagged "{http://www.calaos.fr}option". Match on the local name
            # (after the namespace) rather than the full tag.
            for elem in root.iter():
                if not elem.tag.rsplit("}", 1)[-1] == "option":
                    continue
                name = elem.get("name", "")
                value = elem.get("value", "")
                if name == "mcp_token":
                    mcp_token = value
                elif name == "mcp_service_token":
                    service_token = value
        except (FileNotFoundError, ET.ParseError) as exc:
            raise RuntimeError(f"Cannot read local_config.xml from {config_path}: {exc}") from exc

    if not mcp_token:
        raise RuntimeError("mcp_token not found in local_config.xml")
    if not service_token:
        raise RuntimeError("mcp_service_token not found in local_config.xml")

    # Normalise log level: calaos uses 1-5 integers.
    if log_level.isdigit():
        log_level = {
            "1": "critical", "2": "error", "3": "warning",
            "4": "info", "5": "debug",
        }.get(log_level, "info")

    return Config(
        mcp_token=mcp_token,
        service_token=service_token,
        socket_path=socket_path,
        api_url=api_url,
        log_level=log_level.lower(),
    )
