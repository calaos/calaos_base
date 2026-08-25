#!/bin/sh
# T2.14 -- run the tests/python/ suites (T1.8 pytest suites + T1.16 stdlib
# unittest suites) under "make check".
#
# F-PYTEST-1 (2026-08-25) -- this script used to lie by omission.
#
# It ran pytest "if available" and otherwise fell back to
#     python3 -m unittest discover -p 'test_t116_*.py'
# a pattern that collects 3 of the 6 suites. The remaining 3 -- 19 of the 42
# declared cases -- were never executed, the script exited 0, and automake
# wrote PASS. ⚠️ That is NOT an automake SKIP: a SKIP is counted in the visible
# "# SKIP:" column of the test-suite summary, a PASS on an unexecuted suite is
# indistinguishable from a PASS on a suite that ran. Reading "88/88" told you
# nothing about whether the Python tests had run, and the answer depended on
# the machine.
#
# The rule now is: a test that could not be executed is never reported as PASS.
# tests/python-suite-runner.py counts what tests/python/ DECLARES, counts what
# it actually EXECUTED, publishes both, and picks the exit code from that.
#
# Exit codes follow the automake simple-test protocol:
#   0  -> PASS  (every declared case executed, and passed)
#   77 -> SKIP  (nothing runnable, or some declared case did not execute --
#                visible in the "# SKIP:" column, never a silent PASS)
#   1  -> FAIL  (a python test is red, or a suite errored)
#
# The environment (PYTHON, abs_top_srcdir) is exported by AM_TESTS_ENVIRONMENT
# in tests/Makefile.am; fall back to sane defaults when run by hand.

: "${PYTHON:=python3}"

# AM_PATH_PYTHON sets PYTHON=: when no interpreter was found.
if test "x$PYTHON" = "x:"; then
    echo "SKIP: no python3 interpreter detected at configure time"
    exit 77
fi

if test -z "$abs_top_srcdir"; then
    abs_top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
fi

pydir="$abs_top_srcdir/tests/python"
if test ! -d "$pydir"; then
    echo "SKIP: $pydir not found"
    exit 77
fi

runner="$abs_top_srcdir/tests/python-suite-runner.py"
if test ! -f "$runner"; then
    echo "FAIL: $runner not found"
    exit 1
fi

if ! "$PYTHON" -c "import sys" >/dev/null 2>&1; then
    echo "SKIP: \$PYTHON ($PYTHON) is not runnable"
    exit 77
fi

# Never write bytecode or caches: during "make distcheck" the source tree is
# read-only, and the tests must not leave droppings in srcdir anyway.
PYTHONDONTWRITEBYTECODE=1
export PYTHONDONTWRITEBYTECODE

exec "$PYTHON" "$runner" "$pydir"
