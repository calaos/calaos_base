#!/bin/sh
# T3.48 -- every file under src/ and tests/ must reach the release tarball.
#
# The mirror image of check-extra-dist.sh. That one asks "does every declared
# dist path exist?" (T3.45: a path naming no file kills `make dist`). This one
# asks the opposite and much larger question: "does every file that exists get
# declared?" -- because `make dist` answers "yes, tarball built" while quietly
# leaving 417 tracked files behind, 126 of them C/C++ sources and headers.
# The archive of 2026-08-25 unpacked, configured, and then died on
#   src/lib/ExpressionEvaluator.cpp:2: fatal error: exprtk.hpp: No such file
# because no variable in any Makefile.am ever named the vendored trees.
#
# `make distcheck` is the true oracle and costs minutes; it stays a release
# gate. This is its static stand-in for the reverse direction, and it runs in
# roughly the time python3 needs to start.
#
# Exit codes follow the automake simple-test protocol:
#   0  -> PASS (every scanned file is covered by a dist mechanism)
#   77 -> SKIP (no usable python3)
#   1  -> FAIL (at least one file would be missing from the tarball)
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

exec "$PYTHON" "$abs_top_srcdir/tests/check-dist-coverage.py"
