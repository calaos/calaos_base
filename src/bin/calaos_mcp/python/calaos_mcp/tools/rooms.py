"""MCP tools: list_rooms and get_room."""

from __future__ import annotations

from calaos_mcp import safety
from calaos_mcp.tools._home import (
    device_view, is_writable, iter_rooms, protected_outputs,
)


def _parse_rooms(home: dict) -> list[dict]:
    result = []
    protected = protected_outputs(home)
    for room in iter_rooms(home):
        items = [i for i in room.get("items", []) if i.get("id") not in protected]
        result.append({
            "name": safety.safe_name(room.get("name", "")),
            "type": room.get("type", ""),
            "io_count": len(items),
        })
    return result


def _find_room(home: dict, room_name: str) -> dict | None:
    needle = room_name.lower()
    for room in iter_rooms(home):
        if room.get("name", "").lower() == needle:
            return room
    return None


async def list_rooms(client) -> list[dict]:
    """Return a list of all rooms with their names, types, and IO counts."""
    home = await client.get_home()
    return _parse_rooms(home)


async def get_room(client, room_name: str) -> dict:
    """Return details of a specific room including all its IOs and their current states.

    Args:
        room_name: The name of the room (case-insensitive).
    """
    home = await client.get_home()
    room = _find_room(home, room_name)
    if room is None:
        known = [r.get("name", "") for r in iter_rooms(home)]
        raise ValueError(f"Room '{room_name}' not found. Known rooms: {known}")

    ios = []
    protected = protected_outputs(home)
    for io in room.get("items", []):
        if io.get("id") in protected:
            continue
        entry = {
            "id": io.get("id", ""),
            "name": safety.safe_name(io.get("name", "")),
            "gui_type": io.get("gui_type", ""),
            "var_type": io.get("var_type", ""),
            "state": safety.wrap_untrusted(io.get("state", "")),
            "rw": is_writable(io),
            "unit": io.get("unit", ""),
        }
        device = device_view(io)
        if device is not None:
            entry["device"] = device
        ios.append(entry)
    return {
        "name": safety.safe_name(room.get("name", "")),
        "type": room.get("type", ""),
        "ios": ios,
    }
