"""Shared helpers for traversing the get_home response.

The calaos_server get_home action returns:

    {
      "home": [                         # array of rooms
        {
          "type": "salon",
          "name": "Salon",
          "hits": "0",
          "items": [                     # IOs are under "items", not "ios"
            {"id": "...", "name": "...", "var_type": "bool",
             "gui_type": "...", "state": "false", "rw": "true",
             "io_type": "inout", "unit": "...", ...}
          ]
        }
      ],
      "cameras": [...],
      "audio": [...]
    }

All scalar values (including state, rw) are JSON strings, so callers must
treat "rw" as the string "true"/"false" rather than a boolean.
"""

from __future__ import annotations

from typing import Iterator

from calaos_mcp import safety


def iter_rooms(home: dict) -> Iterator[dict]:
    """Yield each room object from a get_home response."""
    rooms = home.get("home", [])
    if isinstance(rooms, list):
        yield from rooms


def iter_ios(home: dict) -> Iterator[tuple[dict, str]]:
    """Yield (io, room_name) tuples for every IO in the home."""
    for room in iter_rooms(home):
        room_name = room.get("name", "")
        for io in room.get("items", []):
            yield io, room_name


def is_writable(io: dict) -> bool:
    """Return True if the IO can be driven via set_state.

    Writability is determined by io_type, not the rw flag: outputs and inouts
    (lights, shutters, relays...) are always writable. The rw param only exists
    on input value objects (IntValue) as an opt-in "edit mode" flag, so relying
    on it alone wrongly marked every output as read-only.
    """
    io_type = str(io.get("io_type", "")).lower()
    if io_type in ("output", "inout"):
        return True
    # Input value objects (IntValue) opt into writability via the rw flag.
    rw = io.get("rw", "false")
    if isinstance(rw, bool):
        return rw
    return str(rw).lower() == "true"


# impl -> the link roles the device drives itself. A second writer makes the
# regulation fight (spec section 8.3), so MCP never offers these IOs.
_PROTECTED_ROLES = {"thermostat": ("output_heat", "output_cool")}


def protected_outputs(home: dict) -> dict[str, tuple[str, str]]:
    """Map the id of each protected output to (device id, link role).

    Read from the 'device' object of get_home, so it follows the installation
    and not a list of ids kept here.
    """
    protected: dict[str, tuple[str, str]] = {}
    for io, _ in iter_ios(home):
        device = io.get("device")
        if not isinstance(device, dict):
            continue
        ios = device.get("ios")
        if not isinstance(ios, dict):
            continue
        for role in _PROTECTED_ROLES.get(device.get("impl"), ()):
            target = ios.get(role)
            if isinstance(target, str) and target:
                protected[target] = (io.get("id", ""), role)
    return protected


def iter_visible_ios(home: dict) -> Iterator[tuple[dict, str]]:
    """iter_ios without the protected outputs: what the assistant may list."""
    protected = protected_outputs(home)
    for io, room_name in iter_ios(home):
        if io.get("id") not in protected:
            yield io, room_name


def device_view(io: dict) -> dict | None:
    """The 'device' object of an IO, sanitised for the model, None for a plain IO."""
    device = io.get("device")
    if not isinstance(device, dict):
        return None
    return safety.safe_tree(device)
