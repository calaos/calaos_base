#!/bin/sh
# Why this exists: a helper that answers -1 or npos for "not found" turns an
# ordering assertion into a factory for vacuous cases - the comparison accepts
# the sentinel, so the case goes GREEN exactly when the key it guards vanishes
# from the payload. The guard that closes it is one line, it spreads one suite
# at a time, and a case that has lost it looks like every other green. This is
# the only place where the class is closed rather than each member of it.
#
# Exit codes follow the automake simple-test protocol: 0 PASS, 77 SKIP (no
# usable python3), 1 FAIL (at least one accepting slot is ruled out by nothing).
#
# The script self-tests before it scans, on files written to be refused: this
# family of probes has no other exercise, and one that has gone silent looks
# exactly like a clean tree.
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

exec "$PYTHON" "$abs_top_srcdir/tests/check-order-sentinels.py"
