#!/bin/sh
# T3.36 -- every test binary that LINKS a calaos_server object must also
# DEPEND on it.
#
# Why this exists: tests/Makefile.am used to carry, under the comment "Built
# elsewhere, do not let automake turn them into prerequisites", the line
#
#     <name>_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#
# on 65 of its 102 test binaries. The override does what it says -- and what
# it says is that the ~35 to ~58 calaos_server objects the binary links are
# NOT prerequisites of it. make therefore never relinks the test after the
# code under test is rebuilt, and `make check` re-runs the previous binary.
# That produced false GREENs (a reintroduced defect reported PASS) and false
# REDs (a segfault after a rebase, from a test binary linked against a
# pre-rebase object). See docs/refactoring/T3.36.md.
#
# The fix is per-target and lives in tests/Makefile.am. This guard keeps it
# there: every ticket appends its own block to that file, and a block copied
# from an older one would silently reintroduce the blind override. The check
# is static -- it compares each _DEPENDENCIES against its own _LDADD after
# resolving the make variables -- so it costs milliseconds and needs no build.
#
# Exit codes follow the automake simple-test protocol:
#   0  -> PASS (every linked server object is a declared prerequisite)
#   77 -> SKIP (no usable python3)
#   1  -> FAIL (at least one suite links an object it does not depend on)
#
# PYTHON and abs_top_srcdir are exported by AM_TESTS_ENVIRONMENT in
# tests/Makefile.am; fall back to sane defaults when run by hand.

: "${PYTHON:=python3}"

# AM_PATH_PYTHON sets PYTHON=: when no interpreter was found.
if test "x$PYTHON" = "x:"; then
    echo "SKIP: no python3 interpreter detected at configure time"
    exit 77
fi

if test -z "$abs_top_srcdir"; then
    abs_top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
    export abs_top_srcdir
fi

exec "$PYTHON" "$abs_top_srcdir/tests/check-test-deps.py"
