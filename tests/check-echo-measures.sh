#!/bin/sh
# Why this exists: every bound a bounded-echo suite writes is one-sided, so a
# measure that answers "nothing" satisfies all of them at once and the whole
# family goes green with no red anywhere. check-echo-ceilings.sh reads the
# shape of the ceiling and never the measure under it, so it cannot see this.
#
# Exit codes follow the automake simple-test protocol: 0 PASS, 77 SKIP (no
# usable python3), 1 FAIL (at least one measure is pinned by nothing).
#
# The script self-tests before it scans, on files written to be refused and on
# a sweep of an empty tree: this family of probes has no other exercise, and
# one that has gone silent - or that has lost its corpus - looks exactly like a
# clean tree.
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

exec "$PYTHON" "$abs_top_srcdir/tests/check-echo-measures.py"
