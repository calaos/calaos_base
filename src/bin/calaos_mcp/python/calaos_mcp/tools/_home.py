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
    """Return True if the IO is writable. The rw field is a JSON string."""
    rw = io.get("rw", "false")
    if isinstance(rw, bool):
        return rw
    return str(rw).lower() == "true"
