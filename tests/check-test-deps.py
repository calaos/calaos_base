#!/usr/bin/env python3
"""T3.36 -- assert that every object a test binary LINKS is also a
PREREQUISITE of that binary.

Reads tests/Makefile.am, joins the backslash continuations, collects every
variable assignment (``=`` and ``+=``), then, for each entry of
``check_PROGRAMS``, resolves ``<canon>_LDADD`` and ``<canon>_DEPENDENCIES``
down to plain tokens and compares the two sets of ``....$(OBJEXT)`` entries.

Any object present in the link line but absent from the dependency line is a
failure: make cannot know that the binary must be relinked when that object is
rebuilt, so `make check` runs a stale binary against mutated sources.

What this does NOT see
----------------------
* Only ``tests/Makefile.am``. Objects pulled in through the libtool archive
  ``libcalaos_common.la`` are already tracked by automake and are out of scope.
* It compares declarations, not the linker command line. A suite that links an
  object through a variable this parser cannot resolve would be reported as
  linking nothing rather than as an error; the counter printed on success
  (``N binaries, M linking server objects``) is there to make such a silent
  drop visible in the test log.
"""

import collections
import os
import re
import sys

OBJ_SUFFIX = ".$(OBJEXT)"


def load(path):
    """Return (variables, check_programs) from a Makefile.am."""
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().split("\n")

    joined = []
    buf = None
    for line in lines:
        buf = line if buf is None else buf + " " + line.strip()
        if buf.endswith("\\"):
            buf = buf[:-1]
            continue
        joined.append(buf)
        buf = None
    if buf is not None:
        joined.append(buf)

    variables = collections.OrderedDict()
    programs = []
    for line in joined:
        stripped = line.strip()
        if stripped.startswith("#"):
            continue
        match = re.match(r"^([A-Za-z0-9_]+)\s*(\+?=)\s*(.*)$", stripped)
        if not match:
            continue
        name, operator, value = match.groups()
        if name == "check_PROGRAMS":
            programs.extend(value.split())
        if operator == "+=":
            variables[name] = variables.get(name, "") + " " + value
        else:
            variables[name] = value
    return variables, programs


def resolve(value, variables, depth=0):
    """Expand $(VAR) references, leaving $(OBJEXT) and unknown names alone."""
    if depth > 12:
        return value

    def expand(match):
        name = match.group(1)
        if name == "OBJEXT" or name not in variables:
            return match.group(0)
        return resolve(variables[name], variables, depth + 1)

    return re.sub(r"\$\(([A-Za-z0-9_]+)\)", expand, value)


def objects(value, variables):
    return {t for t in resolve(value, variables).split() if t.endswith(OBJ_SUFFIX)}


def canonical(program):
    return re.sub(r"[^A-Za-z0-9_]", "_", program)


def main():
    top = os.environ.get("abs_top_srcdir") or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), ".."
    )
    path = os.path.join(top, "tests", "Makefile.am")
    variables, programs = load(path)

    failures = []
    linking = 0
    for program in programs:
        name = canonical(program)
        linked = objects(variables.get(name + "_LDADD", ""), variables)
        if not linked:
            continue
        linking += 1
        declared = objects(variables.get(name + "_DEPENDENCIES", ""), variables)
        missing = linked - declared
        if missing:
            failures.append((program, sorted(missing)))

    if failures:
        print("FAIL: %d test binaries link server objects they do not depend on."
              % len(failures))
        print("      make will not relink them when those objects are rebuilt,")
        print("      so `make check` re-runs a stale binary. See T3.36.")
        for program, missing in failures:
            print("  %s: %d object(s) not in %s_DEPENDENCIES, e.g."
                  % (program, len(missing), canonical(program)))
            for obj in missing[:3]:
                print("      %s" % obj)
        return 1

    print("PASS: %d check_PROGRAMS, %d of them link calaos_server objects,"
          % (len(programs), linking))
    print("      and every linked object is a declared prerequisite.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
