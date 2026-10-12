#!/bin/sh
# Self-test of scripts/check-diff-coverage.py, the PR diff-coverage gate: the
# gate's verdicts on synthetic tracefiles and diffs, then counter-mutations
# of the script that this suite must turn red.
#
# Exit codes follow the automake simple-test protocol: 0 PASS, 77 SKIP (no
# usable python3), 1 FAIL.

: "${PYTHON:=python3}"

# AM_PATH_PYTHON sets PYTHON=: when no interpreter was found.
if test "x$PYTHON" = "x:"; then
    echo "SKIP: no python3 interpreter detected at configure time"
    exit 77
fi

if test -z "$abs_top_srcdir"; then
    abs_top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
fi

PYTHONDONTWRITEBYTECODE=1
export PYTHONDONTWRITEBYTECODE
exec "$PYTHON" "$abs_top_srcdir/tests/check-diff-coverage-selftest.py"
