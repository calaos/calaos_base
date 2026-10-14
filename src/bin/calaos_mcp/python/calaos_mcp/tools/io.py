"""MCP tools: list_ios, find_io, get_io_state, set_io_state."""

from __future__ import annotations

import json
import math
import re

from calaos_mcp import iodoc, safety
from calaos_mcp.tools._home import (
    device_view, is_writable, iter_ios, iter_visible_ios, protected_outputs,
)

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
            num = float(value)
        except ValueError:
            raise ValueError(f"Expected a number for IO {io.get('id')!r}, got {value!r}")
        # float() accepts 'inf', 'nan', '1e999' (overflows to inf) — none of
        # which are valid IO states to forward to calaos_server.
        if not math.isfinite(num):
            raise ValueError(f"Expected a finite number for IO {io.get('id')!r}, got {value!r}")
        if var_type == "int" and not num.is_integer():
            raise ValueError(f"Expected an integer for IO {io.get('id')!r}, got {value!r}")
        return value
    # string: sanitise
    if not re.match(r'^[\x20-\x7E\x80-\xFF]*$', value):
        raise ValueError("Value contains disallowed characters")
    if len(value) > 256:
        raise ValueError("String value too long (max 256 chars)")
    return value


def _with_device(entry: dict, io: dict) -> dict:
    device = device_view(io)
    if device is not None:
        entry["device"] = device
    return entry


async def list_ios(client, room: str | None = None, gui_type: str | None = None) -> list[dict]:
    """Return a list of IOs, optionally filtered by room name and/or gui_type.

    Args:
        room: Optional room name filter (case-insensitive).
        gui_type: Optional gui_type filter, e.g. 'light', 'shutter', 'sensor'.
    """
    home = await client.get_home()
    result = []
    room_filter = room.lower() if room else None
    for io, room_name in iter_visible_ios(home):
        if room_filter and room_name.lower() != room_filter:
            continue
        if gui_type and io.get("gui_type", "") != gui_type:
            continue
        result.append(_with_device({
            "id": io.get("id", ""),
            "name": safety.safe_name(io.get("name", "")),
            "gui_type": io.get("gui_type", ""),
            "var_type": io.get("var_type", ""),
            "room": safety.safe_name(room_name),
            "rw": is_writable(io),
            "unit": io.get("unit", ""),
            "state": safety.wrap_untrusted(io.get("state", "")),
        }, io))
    return result


async def find_io(client, query: str) -> list[dict]:
    """Fuzzy-search IOs by name or id. Returns up to 5 best matches.

    Args:
        query: Search string matched against IO names and IDs.
    """
    home = await client.get_home()
    needle = query.lower()
    scored = []
    for io, room_name in iter_visible_ios(home):
        name = io.get("name", "").lower()
        io_id = io.get("id", "").lower()
        if needle in name or needle in io_id:
            # Exact match scores higher than partial
            score = 2 if needle == name or needle == io_id else 1
            scored.append((score, _with_device({
                "id": io.get("id", ""),
                "name": safety.safe_name(io.get("name", "")),
                "gui_type": io.get("gui_type", ""),
                "room": safety.safe_name(room_name),
                "rw": is_writable(io),
                "state": safety.wrap_untrusted(io.get("state", "")),
            }, io)))
    scored.sort(key=lambda x: -x[0])
    return [item for _, item in scored[:5]]


async def get_io_state(client, io_id: str) -> dict:
    """Get the current state of a single IO by its ID.

    A device (climate) also returns its 'device' object: capabilities, state
    fields and the ids of its linked IOs.

    Args:
        io_id: The unique IO identifier.
    """
    # get_state returns a flat mapping {io_id: state_value}.
    result = await client.get_state([io_id])
    if io_id not in result:
        raise ValueError(f"IO '{io_id}' not found or returned no state")
    entry = {
        "id": io_id,
        "state": safety.wrap_untrusted(result[io_id]),
    }
    home = await client.get_home()
    for io, _ in iter_ios(home):
        if io.get("id") == io_id:
            return _with_device(entry, io)
    return entry


def _protected_error(io_id: str, device_id: str, role: str) -> ValueError:
    return ValueError(
        f"IO '{io_id}' is the {role} of the thermostat '{device_id}' and cannot "
        f"be written directly: a second writer would fight the regulation. "
        f"Command the thermostat instead, e.g. set_io_state('{device_id}', "
        f"'{{\"mode\":\"off\"}}').")


# Command field -> the capability the instance must publish for it to exist.
_FIELD_CAPABILITY = {
    "mode": "modes",
    "target_temp": "target_temp",
    "target_temp_low": "target_temp_range",
    "target_temp_high": "target_temp_range",
    "fan_mode": "fan_modes",
    "swing_mode": "swing_modes",
}
# Command field -> the capability that lists its allowed values.
_ENUM_FIELDS = {"mode": "modes", "fan_mode": "fan_modes", "swing_mode": "swing_modes"}


def _validate_device_command(value: str, io: dict, known_fields: set[str]) -> str:
    """Check a device command and return it in the compact form the server parses.

    The server stays the authority on bounds (it clamps) and on what an instance
    accepts; this only refuses what it would silently drop, so the model learns
    about it instead of reading a success.
    """
    device = io["device"]
    caps = device.get("capabilities")
    caps = caps if isinstance(caps, dict) else {}
    io_id = io.get("id")
    text = value.strip()

    if not text.startswith("{"):
        modes = caps.get("modes")
        if modes is not None and text not in modes:
            raise ValueError(
                f"Mode {value!r} is not offered by {io_id!r}. Available modes: {modes}")
        return _validate_value(text, {**io, "var_type": "string"})

    def reject_constant(name: str):
        raise ValueError(f"Invalid number {name} in the command for {io_id!r}")

    try:
        command = json.loads(text, parse_constant=reject_constant)
    except json.JSONDecodeError as exc:
        raise ValueError(f"The command for {io_id!r} is not valid JSON: {exc}") from exc
    if not isinstance(command, dict) or not command:
        raise ValueError(f"The command for {io_id!r} must be a non-empty JSON object")

    for name, field in command.items():
        if known_fields and name not in known_fields:
            raise ValueError(
                f"Unknown field {name!r} for {io_id!r}. Known fields: {sorted(known_fields)}")
        needed = _FIELD_CAPABILITY.get(name)
        if needed is not None and not caps.get(needed):
            raise ValueError(
                f"{io_id!r} has no {needed} capability: the field {name!r} is not "
                f"accepted by this device. Capabilities: {sorted(caps)}")
        if isinstance(field, bool) or not isinstance(field, (str, int, float)):
            raise ValueError(f"Field {name!r} must be a string or a number")
        listed = caps.get(_ENUM_FIELDS.get(name, ""))
        if listed is not None and field not in listed:
            raise ValueError(
                f"{field!r} is not available for {name!r} on {io_id!r}. Available: {listed}")
    return _validate_value(
        json.dumps(command, separators=(",", ":")), {**io, "var_type": "string"})


async def set_io_state(client, io_id: str, value: str) -> dict:
    """Set the state of a writable IO.

    Args:
        io_id: The unique IO identifier. Must be a writable (rw=true) IO.
        value: New value. For bool IOs: 'true'/'false'. For numeric: a number.
    """
    # A write is the one place where a stale topology is not acceptable: the
    # protected outputs come from this very read.
    home = await client.get_home(force=True)
    target_io: dict | None = None
    for io, _ in iter_ios(home):
        if io.get("id") == io_id:
            target_io = io
            break
    if target_io is None:
        raise ValueError(f"IO '{io_id}' not found")
    protected = protected_outputs(home).get(io_id)
    if protected is not None:
        raise _protected_error(io_id, *protected)
    if not is_writable(target_io):
        raise ValueError(f"IO '{io_id}' is read-only")

    if isinstance(target_io.get("device"), dict):
        validated = _validate_device_command(
            value, target_io, iodoc.command_fields(iodoc.load_devices()))
    else:
        validated = _validate_value(value, target_io)
    result = await client.set_state(io_id, validated)
    return {"id": io_id, "value_sent": validated, "result": result}
