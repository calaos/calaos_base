#!/bin/sh
# Why this exists: the shape that keeps a bounded-echo ceiling honest - the
# suite re-measures the incidental overlap and pins `ceiling == worst + 1` -
# spreads one suite at a time, and every suite written in between carries a
# constant nobody revisits. This is the only place where the class is closed
# rather than each member of it.
#
# Exit codes follow the automake simple-test protocol: 0 PASS, 77 SKIP (no
# usable python3), 1 FAIL (at least one ceiling is re-derived by nothing).
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

exec "$PYTHON" "$abs_top_srcdir/tests/check-echo-ceilings.py"
