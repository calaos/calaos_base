"""Device command documentation, read from the io_doc.json IODoc publishes.

calaos_server --gendoc writes it (the build runs it into src/bin/calaos_mcp
and installs it next to the wrapper). The command format of a device is never
restated here: the set_io_state tool description is rendered from it.
"""

from __future__ import annotations

import json
import logging
import os

LOG = logging.getLogger("calaos_mcp.iodoc")

ENV_PATH = "CALAOS_IODOC_PATH"

# Key of io_doc.json that holds the devices, by impl. It is not an IO type.
_DEVICES_KEY = "devices"


_cache: dict[tuple[str, int], dict[str, dict]] = {}


def load_devices(path: str | None = None) -> dict[str, dict]:
    """Return {impl: doc} for the devices of io_doc.json, {} when unavailable.

    The sidecar must start without it: a missing or broken file only costs the
    generated part of the tool description. Parsed once per version of the file,
    since set_io_state asks on every write.
    """
    path = path or os.environ.get(ENV_PATH, "")
    if not path:
        return {}
    try:
        key = (path, os.stat(path).st_mtime_ns)
        if key not in _cache:
            _cache.clear()
            _cache[key] = _parse(path)
        return _cache[key]
    except (OSError, ValueError) as exc:
        LOG.warning("device command documentation unavailable (%s): %s", path, exc)
        return {}


def _parse(path: str) -> dict[str, dict]:
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    devices = doc.get(_DEVICES_KEY) if isinstance(doc, dict) else None
    if not isinstance(devices, dict) or not devices:
        raise ValueError("no device in the file")
    return {impl: d for impl, d in devices.items()
            if isinstance(d, dict) and d.get("command_format") == "json"}


def command_fields(devices: dict[str, dict]) -> set[str]:
    return {f["name"] for d in devices.values()
            for f in d.get("command_fields", []) if "name" in f}


def _num(v: float) -> str:
    return str(int(v)) if float(v).is_integer() else str(v)


def _field_line(f: dict) -> str:
    kind = f.get("type", "")
    if kind == "enum":
        if "values" in f:
            kind = "one of " + ", ".join(f["values"])
        elif "values_param" in f:
            kind = (f"one of the values of the '{f['values_param']}' param "
                    f"(listed in device.capabilities)")
    elif kind == "number":
        kind = f"number from {_num(f['min'])} to {_num(f['max'])}"
        if "step" in f:
            kind += f" by {_num(f['step'])}"
        if f.get("unit"):
            kind += f" {f['unit']}"
    line = f"  - {f.get('name', '?')}: {kind}."
    if f.get("description"):
        line += " " + f["description"]
    links = f.get("requires_links") or []
    if links:
        line += " Needs the link " + ", ".join(links) + "."
    for value, needed in sorted((f.get("value_requires_links") or {}).items()):
        line += f" The value {value} also needs the link {', '.join(needed)}."
    return line


def example(devices: dict[str, dict]) -> str:
    """A command made of the first fields of the first impl, always valid."""
    parts = []
    for d in devices.values():
        for f in d.get("command_fields", []):
            if len(parts) == 2:
                break
            if f.get("type") == "enum" and f.get("values"):
                values = [v for v in f["values"] if v != "off"] or f["values"]
                parts.append((f["name"], values[0]))
            elif f.get("type") == "number" and "min" in f and "max" in f:
                mid = (f["min"] + f["max"]) / 2
                step = f.get("step") or 0
                if step:
                    mid = f["min"] + round((mid - f["min"]) / step) * step
                parts.append((f["name"], int(mid) if float(mid).is_integer() else mid))
        break
    return json.dumps(dict(parts), separators=(",", ":"))


def command_help(devices: dict[str, dict]) -> str:
    """The part of the set_io_state description that documents device commands."""
    if not devices:
        return ""
    lines = [
        "Devices are the IOs that carry a 'device' object in list_ios, get_room "
        "and get_io_state. Their value is either a plain mode ('off') or a JSON "
        f"object given as a string, a partial update such as '{example(devices)}'. "
        "Only the fields present are applied. Read device.capabilities first: a "
        "field the instance has no capability for is refused.",
    ]
    for impl, d in sorted(devices.items()):
        head = f"Implementation '{impl}'"
        if d.get("family"):
            head += f" ({d['family']})"
        lines.append(head + ": " + d.get("description", ""))
        lines.extend(_field_line(f) for f in d.get("command_fields", []))
    return "\n".join(lines)
