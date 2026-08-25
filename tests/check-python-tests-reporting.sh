#!/bin/sh
#
#  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
#
#  This file is part of Calaos.
#
#  Calaos is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  Calaos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with Calaos. If not, see <http://www.gnu.org/licenses/>.
#
# ----------------------------------------------------------------------------
# META-ORACLE for run-python-tests.sh  (F-PYTEST-1)
#
# This test does not test the product. It tests the *reporting honesty* of
# another entry of TESTS.
#
# The defect it exists for: run-python-tests.sh used to run pytest "if
# available" and, when it was not, fall back to
#     python3 -m unittest discover -p 'test_t116_*.py'
# which collects 3 of the 6 suites in tests/python/. The other 3 (19 of the 42
# declared cases) were never executed, the script still exited 0, automake
# wrote PASS in the .trs, and the only trace was one line in a .log nobody
# reads. That is NOT an automake SKIP -- a SKIP is counted in the visible
# "# SKIP:" column of the test-suite summary. A PASS on an unexecuted suite is
# indistinguishable from a PASS on a suite that ran.
#
# ⚠️ What is asserted here is deliberately NOT "is pytest installed". The
# defect is not the missing dependency, it is the SILENCE. A machine with
# every dependency and a machine with none must BOTH end up telling the truth;
# only the verdict (PASS vs SKIP) may differ.
#
# The four cases below are pairwise independent and each one pins a distinct
# property. C3 and C4 are the CONTROLS: they already hold before the fix, so a
# harness that went red everywhere would be visible immediately.
#
#   C1  accounting        the launcher must PUBLISH how many declared cases it
#                         actually executed, in a machine-readable line, and
#                         its exit code must agree with that line
#                         (all executed -> 0, some not executed -> 77)
#   C2  no-pytest         with pytest made unimportable, the launcher must NOT
#                         report success: the 3 pytest-only suites cannot run,
#                         so the honest verdict is 77 (SKIP), never 0 (PASS)
#   C3  no-interpreter    PYTHON=: -> 77                        [CONTROL]
#   C4  no-suite-dir      tests/python/ missing -> 77           [CONTROL]
#
# Exit codes: 0 = PASS, 1 = FAIL. This test never emits 77: it needs nothing
# but /bin/sh and the same $PYTHON the launcher itself needs, and when there is
# no usable $PYTHON at all the launcher's own contract (C3) is still checkable.
# ----------------------------------------------------------------------------

set -u

scriptdir=`dirname "$0"`
scriptdir=`cd "$scriptdir" && pwd`

: ${abs_top_srcdir:="$scriptdir/.."}
abs_top_srcdir=`cd "$abs_top_srcdir" && pwd`

LAUNCHER="$abs_top_srcdir/tests/run-python-tests.sh"
PYDIR="$abs_top_srcdir/tests/python"

nb_fail=0

fail()
{
    echo "FAIL: $*" >&2
    nb_fail=`expr $nb_fail + 1`
}

if [ ! -f "$LAUNCHER" ]; then
    echo "FAIL: $LAUNCHER is missing" >&2
    exit 1
fi
if [ ! -d "$PYDIR" ]; then
    echo "FAIL: $PYDIR is missing" >&2
    exit 1
fi

tmpdir=`mktemp -d 2>/dev/null` || tmpdir=/tmp/check-python-tests-reporting.$$
mkdir -p "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

# ---------------------------------------------------------------------------
# How many test cases does tests/python/ DECLARE? Counted from the sources,
# never from what the launcher chose to run -- the whole point is that the two
# numbers may disagree.
#
# This uses the ambient python3 (not $PYTHON, which C3 may have set to ":"):
# it is a property of the source tree, not of the launcher's environment.
# ---------------------------------------------------------------------------
COUNTER="$tmpdir/count_declared.py"
cat > "$COUNTER" <<'PY_EOF'
import ast
import os
import sys

pydir = sys.argv[1]
files = 0
cases = 0
for name in sorted(os.listdir(pydir)):
    if not (name.startswith("test_") and name.endswith(".py")):
        continue
    files += 1
    tree = ast.parse(open(os.path.join(pydir, name), encoding="utf-8").read(), name)

    def walk(node):
        n = 0
        for child in node.body:
            if isinstance(child, ast.ClassDef):
                n += walk(child)
            elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                if child.name.startswith("test"):
                    n += 1
        return n

    cases += walk(tree)
print("%d %d" % (files, cases))
PY_EOF

declared=`python3 "$COUNTER" "$PYDIR" 2>/dev/null` || declared=""
if [ -z "$declared" ]; then
    echo "FAIL: could not count the declared python cases (no usable python3?)" >&2
    exit 1
fi
declared_files=`echo "$declared" | cut -d' ' -f1`
declared_cases=`echo "$declared" | cut -d' ' -f2`

if [ "$declared_files" -lt 1 ] || [ "$declared_cases" -lt 1 ]; then
    fail "the fixture is empty: $declared_files files / $declared_cases cases declared" \
         "in $PYDIR -- this harness would pass vacuously"
fi

echo "check-python-tests-reporting: tests/python/ declares" \
     "$declared_files suites / $declared_cases cases"

# ---------------------------------------------------------------------------
# C1 -- the launcher must publish an accounting line, and its exit code must
#       agree with it.
#
# Contract:
#   run-python-tests: suites=<ran>/<declared> cases=<executed>/<declared>
#
# and then:  executed == declared  <=> exit 0
#            executed <  declared  <=> exit 77 (automake SKIP, visible)
#            a red case            <=> exit 1
# ---------------------------------------------------------------------------
c1log="$tmpdir/c1.log"
(
    abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
    "$LAUNCHER"
) >"$c1log" 2>&1
c1rc=$?

line=`sed -n 's/^run-python-tests: \(suites=.*\)$/\1/p' "$c1log" | tail -n 1`
if [ -z "$line" ]; then
    fail "C1: run-python-tests.sh printed no 'run-python-tests: suites=N/M cases=N/M'" \
         "accounting line (exit code was $c1rc)." \
         "Without it nothing distinguishes a suite that ran from a suite that did not."
else
    got_suites=`echo "$line" | sed -n 's/.*suites=\([0-9]*\)\/\([0-9]*\).*/\1/p'`
    tot_suites=`echo "$line" | sed -n 's/.*suites=\([0-9]*\)\/\([0-9]*\).*/\2/p'`
    got_cases=`echo "$line" | sed -n 's/.*cases=\([0-9]*\)\/\([0-9]*\).*/\1/p'`
    tot_cases=`echo "$line" | sed -n 's/.*cases=\([0-9]*\)\/\([0-9]*\).*/\2/p'`

    if [ -z "$got_cases" ] || [ -z "$tot_cases" ]; then
        fail "C1: malformed accounting line: '$line'"
    else
        if [ "$tot_suites" -ne "$declared_files" ] || [ "$tot_cases" -ne "$declared_cases" ]; then
            fail "C1: the launcher declares $tot_suites/$tot_cases but the sources declare" \
                 "$declared_files/$declared_cases -- the launcher is counting something else"
        fi
        if [ "$got_cases" -eq "$tot_cases" ]; then
            if [ "$c1rc" -ne 0 ] && [ "$c1rc" -ne 1 ]; then
                fail "C1: every declared case ran ($line) but the launcher exited $c1rc"
            fi
        else
            if [ "$c1rc" -ne 77 ] && [ "$c1rc" -ne 1 ]; then
                fail "C1: only $got_cases of $tot_cases declared cases ran, yet the launcher" \
                     "exited $c1rc. An unexecuted case must never be reported as PASS;" \
                     "the honest codes are 77 (SKIP) or 1 (FAIL)."
            fi
        fi
    fi
fi

# ---------------------------------------------------------------------------
# C2 -- pytest made unimportable. Three of the six suites then CANNOT run
#       (they import pytest at module level). The launcher must not say PASS.
#
# The shadow module makes this deterministic on a machine that has pytest and
# on a machine that has not: both are pushed into the same state.
# ---------------------------------------------------------------------------
shadow="$tmpdir/shadow"
mkdir -p "$shadow" || exit 1
cat > "$shadow/pytest.py" <<'PY_EOF'
raise ImportError("pytest hidden by tests/check-python-tests-reporting.sh (C2)")
PY_EOF

stub="$tmpdir/python3-nopytest"
cat > "$stub" <<STUB_EOF
#!/bin/sh
PYTHONPATH="$shadow\${PYTHONPATH:+:\$PYTHONPATH}"
export PYTHONPATH
exec ${PYTHON:-python3} "\$@"
STUB_EOF
chmod +x "$stub"

if ! "$stub" -c "import sys" >/dev/null 2>&1; then
    echo "check-python-tests-reporting: C2 skipped, no runnable interpreter"
else
    if "$stub" -c "import pytest" >/dev/null 2>&1; then
        fail "C2: the pytest shadow did not take effect -- the case would be vacuous"
    else
        c2log="$tmpdir/c2.log"
        (
            abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
            PYTHON="$stub"; export PYTHON
            "$LAUNCHER"
        ) >"$c2log" 2>&1
        c2rc=$?
        if [ "$c2rc" -eq 0 ]; then
            fail "C2: pytest is unimportable, so test_auth.py, test_extern_proc.py and" \
                 "test_logger.py cannot run at all -- yet run-python-tests.sh exited 0" \
                 "and automake will write PASS. This is the F-PYTEST-1 false green."
            sed 's/^/      /' "$c2log" >&2
        elif [ "$c2rc" -ne 77 ] && [ "$c2rc" -ne 1 ]; then
            fail "C2: unexpected exit code $c2rc (expected 77 = SKIP, or 1 = FAIL)"
        fi
    fi
fi

# ---------------------------------------------------------------------------
# C3 -- CONTROL. AM_PATH_PYTHON sets PYTHON=: when there is no interpreter.
#       The launcher already handles this correctly; it must keep doing so.
# ---------------------------------------------------------------------------
c3log="$tmpdir/c3.log"
(
    abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
    PYTHON=:; export PYTHON
    "$LAUNCHER"
) >"$c3log" 2>&1
c3rc=$?
if [ "$c3rc" -ne 77 ]; then
    fail "C3 (control): PYTHON=: must give 77 (SKIP), got $c3rc"
fi

# ---------------------------------------------------------------------------
# C4 -- CONTROL. No tests/python/ at all -> 77, never 0.
# ---------------------------------------------------------------------------
emptytree="$tmpdir/emptytree"
mkdir -p "$emptytree/tests" || exit 1
c4log="$tmpdir/c4.log"
(
    abs_top_srcdir="$emptytree"; export abs_top_srcdir
    "$LAUNCHER"
) >"$c4log" 2>&1
c4rc=$?
if [ "$c4rc" -ne 77 ]; then
    fail "C4 (control): a missing tests/python/ must give 77 (SKIP), got $c4rc"
fi

if [ "$nb_fail" -ne 0 ]; then
    echo "" >&2
    echo "check-python-tests-reporting: $nb_fail check(s) failed." >&2
    exit 1
fi

echo "check-python-tests-reporting: run-python-tests.sh reports honestly (4 checks)"
exit 0
