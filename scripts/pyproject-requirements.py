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

With --extra NAME (repeatable) the named [project.optional-dependencies] group
is appended to the same list, so that one resolver pass still sees the whole
set. An unknown extra is an error, not an empty list: a typo must not quietly
install less than the caller asked for.

Usage: pyproject-requirements.py [--extra NAME]... <path/to/pyproject.toml>
"""

import sys
import tomllib


def main(argv):
    extras = []
    args = []
    rest = argv[1:]
    while rest:
        arg = rest.pop(0)
        if arg == "--extra":
            if not rest:
                sys.stderr.write("--extra needs a name\n")
                return 2
            extras.append(rest.pop(0))
        elif arg.startswith("--extra="):
            extras.append(arg.split("=", 1)[1])
        else:
            args.append(arg)

    if len(args) != 1:
        sys.stderr.write("usage: %s [--extra NAME]... <pyproject.toml>\n" % argv[0])
        return 2

    with open(args[0], "rb") as fd:
        data = tomllib.load(fd)

    project = data.get("project", {})
    deps = list(project.get("dependencies", []))
    if not deps:
        sys.stderr.write("%s declares no [project].dependencies\n" % args[0])
        return 1

    optional = project.get("optional-dependencies", {})
    for extra in extras:
        if extra not in optional:
            sys.stderr.write("%s declares no [project.optional-dependencies].%s\n"
                             % (args[0], extra))
            return 1
        deps.extend(optional[extra])

    for dep in deps:
        print(dep)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
