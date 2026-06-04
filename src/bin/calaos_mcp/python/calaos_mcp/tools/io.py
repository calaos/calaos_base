"""MCP tools: list_ios, find_io, get_io_state, set_io_state."""

from __future__ import annotations

import re

from calaos_mcp import safety
from calaos_mcp.tools._home import iter_ios, is_writable

# Allowed cast per var_type (S14)
_BOOL_VALUES = frozenset({"true", "false", "1", "0", "on", "off"})


def _normalise_bool(v: str) -> str:
    low = v.lower()
    if low in ("true", "1", "on"):
        return "true"
    if low in ("false", "0", "off"):
        return "false"
    raise ValueError(f"Invalid boolean value: {v!r}. Use true/false.")


def _validate_value(value: str, io: dict) -> str:
    var_type = io.get("var_type", "bool")
    if var_type == "bool":
        return _normalise_bool(value)
    if var_type in ("float", "int", "double"):
        try:
            float(value)
        except ValueError:
            raise ValueError(f"Expected a number for IO {io.get('id')!r}, got {value!r}")
        return value
    # string: sanitise
    if not re.match(r'^[\x20-\x7E\x80-\xFF]*$', value):
        raise ValueError("Value contains disallowed characters")
    if len(value) > 256:
        raise ValueError("String value too long (max 256 chars)")
    return value


async def list_ios(client, room: str | None = None, gui_type: str | None = None) -> list[dict]:
    """Return a list of IOs, optionally filtered by room name and/or gui_type.

    Args:
        room: Optional room name filter (case-insensitive).
        gui_type: Optional gui_type filter, e.g. 'light', 'shutter', 'sensor'.
    """
    home = await client.get_home()
    result = []
    room_filter = room.lower() if room else None
    for io, room_name in iter_ios(home):
        if room_filter and room_name.lower() != room_filter:
            continue
        if gui_type and io.get("gui_type", "") != gui_type:
            continue
        result.append({
            "id": io.get("id", ""),
            "name": safety.safe_name(io.get("name", "")),
            "gui_type": io.get("gui_type", ""),
            "var_type": io.get("var_type", ""),
            "room": safety.safe_name(room_name),
            "rw": is_writable(io),
            "unit": io.get("unit", ""),
            "state": safety.wrap_untrusted(io.get("state", "")),
        })
    return result


async def find_io(client, query: str) -> list[dict]:
    """Fuzzy-search IOs by name or id. Returns up to 5 best matches.

    Args:
        query: Search string matched against IO names and IDs.
    """
    home = await client.get_home()
    needle = query.lower()
    scored = []
    for io, room_name in iter_ios(home):
        name = io.get("name", "").lower()
        io_id = io.get("id", "").lower()
        if needle in name or needle in io_id:
            # Exact match scores higher than partial
            score = 2 if needle == name or needle == io_id else 1
            scored.append((score, {
                "id": io.get("id", ""),
                "name": safety.safe_name(io.get("name", "")),
                "gui_type": io.get("gui_type", ""),
                "room": safety.safe_name(room_name),
                "rw": is_writable(io),
                "state": safety.wrap_untrusted(io.get("state", "")),
            }))
    scored.sort(key=lambda x: -x[0])
    return [item for _, item in scored[:5]]


async def get_io_state(client, io_id: str) -> dict:
    """Get the current state of a single IO by its ID.

    Args:
        io_id: The unique IO identifier.
    """
    # get_state returns a flat mapping {io_id: state_value}.
    result = await client.get_state([io_id])
    if io_id not in result:
        raise ValueError(f"IO '{io_id}' not found or returned no state")
    return {
        "id": io_id,
        "state": safety.wrap_untrusted(result[io_id]),
    }


async def set_io_state(client, io_id: str, value: str) -> dict:
    """Set the state of a writable IO.

    Args:
        io_id: The unique IO identifier. Must be a writable (rw=true) IO.
        value: New value. For bool IOs: 'true'/'false'. For numeric: a number.
    """
    # Resolve IO metadata for validation
    home = await client.get_home()
    target_io: dict | None = None
    for io, _ in iter_ios(home):
        if io.get("id") == io_id:
            target_io = io
            break
    if target_io is None:
        raise ValueError(f"IO '{io_id}' not found")
    if not is_writable(target_io):
        raise ValueError(f"IO '{io_id}' is read-only")

    validated = _validate_value(value, target_io)
    result = await client.set_state(io_id, validated)
    return {"id": io_id, "value_sent": validated, "result": result}
