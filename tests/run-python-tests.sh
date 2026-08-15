#!/bin/sh
# T2.14 -- run the tests/python/ suites (T1.8 pytest suites + T1.16 stdlib
# unittest suites) under "make check".
#
# Exit codes follow the automake simple-test protocol:
#   0  -> PASS (every test that could run passed)
#   77 -> SKIP (no usable python3; make check stays green)
#   1  -> FAIL (a python test is red, or a suite errored)
#
# Skip semantics:
#   - $PYTHON absent/unusable                    -> exit 77 (whole suite SKIP)
#   - pytest available                           -> run everything with pytest;
#     per-file deps (fastapi/httpx/colorama) are handled by
#     pytest.importorskip inside the tests themselves.
#   - pytest missing                             -> fall back to stdlib
#     "python3 -m unittest discover" on the test_t116_*.py files and skip the
#     pytest-only files with a clear message (still PASS/FAIL on the rest).
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

if ! "$PYTHON" -c "import sys" >/dev/null 2>&1; then
    echo "SKIP: \$PYTHON ($PYTHON) is not runnable"
    exit 77
fi

# Never write bytecode or caches: during "make distcheck" the source tree is
# read-only, and the tests must not leave droppings in srcdir anyway.
PYTHONDONTWRITEBYTECODE=1
export PYTHONDONTWRITEBYTECODE

if "$PYTHON" -m pytest --version >/dev/null 2>&1; then
    echo "Running python suites with pytest ($PYTHON)"
    exec "$PYTHON" -m pytest -q -p no:cacheprovider "$pydir"
fi

# No pytest: run the stdlib-unittest suites (test_t116_*.py need nothing
# outside the standard library) and skip the pytest-only files.
echo "pytest not available for $PYTHON:"
echo "  skipping pytest-only suites (test_auth.py, test_extern_proc.py, test_logger.py)"
echo "  running stdlib unittest suites (test_t116_*.py) instead"
exec "$PYTHON" -m unittest discover -v -s "$pydir" -p 'test_t116_*.py'
