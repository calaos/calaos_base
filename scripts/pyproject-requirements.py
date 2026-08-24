#!/usr/bin/env python3
"""Print the runtime dependencies declared in a pyproject.toml, one per line.

Single source of truth for the Python dependencies of the calaos_mcp sidecar.

Historically the Dockerfiles ran `pip install "mcp[cli]" uvicorn fastapi
websockets` with no version bound at all, while
src/bin/calaos_mcp/pyproject.toml carried a fully pinned set that no build path
ever read. The pins were documentation, not a contract: on 2026-08-24 a rebuild
resolved to mcp 2.0.0, where mcp.server.fastmcp no longer exists, and the
published image shipped a sidecar that could not import its own entry point.

Emitting the pyproject dependencies as a requirements list makes the file
Dependabot watches be exactly the file the image installs. Feed the output to a
single `pip install -r`, so the resolver sees the whole set at once: mcp,
fastapi and starlette are tightly coupled and resolving them one package at a
time silently picks incompatible combinations.

Usage: pyproject-requirements.py <path/to/pyproject.toml>
"""

import sys
import tomllib


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: %s <pyproject.toml>\n" % argv[0])
        return 2

    with open(argv[1], "rb") as fd:
        data = tomllib.load(fd)

    deps = data.get("project", {}).get("dependencies", [])
    if not deps:
        sys.stderr.write("%s declares no [project].dependencies\n" % argv[1])
        return 1

    for dep in deps:
        print(dep)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
