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
#   C1  accounting        the launcher must PUBLISH how many declared cases it
#                         actually executed, in a machine-readable line, and
#                         its exit code must agree with that line
#                         (all executed -> 0, some not executed -> 77)
#   C2  no-pytest         with pytest made unimportable, the launcher must
#                         still publish a consistent accounting line, and must
#                         not claim MORE executed cases than with pytest.
#                         ⚠️ It asserts nothing about how many suites of the
#                         fixture happen to need pytest: porting them all to
#                         stdlib unittest is a desirable change and must not
#                         turn this case red.
#   C3  no-interpreter    PYTHON=: -> 77                        [CONTROL]
#   C4  no-suite-dir      tests/python/ missing -> 77           [CONTROL]
#   C5  skipped-is-not-executed
#                         on a FABRICATED tree of exactly two declared cases,
#                         one of which is skipped from the inside, the
#                         launcher must report cases=1/2 and exit 77 -- never
#                         2/2 and 0. Run twice: once through the ambient
#                         interpreter (pytest back-end when pytest is there)
#                         and once with pytest shadowed (unittest back-end),
#                         so BOTH back-ends are pinned. A second fabricated
#                         tree pins the parametrization trap: 1 parametrized
#                         case expanding to 3 junit entries must not pay for
#                         the 1 skipped case next to it.
#
# C3 and C4 are the CONTROLS: they already hold before the fix, so a harness
# that went red everywhere would be visible immediately.
#
# ⚠️ WHAT THIS HARNESS NEEDS. C3 and C4 need nothing but /bin/sh. C1, C2 and
# C5 need an interpreter able to parse the sources and count what they
# declare. On an image with NO python at all (the CI image of
# .github/workflows/ci.yml is exactly that: its apt list has no python3), that
# counting is impossible, so this test exits 77 -- an automake SKIP, counted
# in the visible "# SKIP:" column. It must NEVER exit 1 there: a harness that
# cannot measure has found no defect, and reporting one would be the very sin
# it exists to catch, one level up.
#
# Exit codes: 0 = PASS, 1 = FAIL, 77 = SKIP (no interpreter to count with).
# ----------------------------------------------------------------------------

set -u

scriptdir=`dirname "$0"`
scriptdir=`cd "$scriptdir" && pwd`

: ${abs_top_srcdir:="$scriptdir/.."}
abs_top_srcdir=`cd "$abs_top_srcdir" && pwd`

LAUNCHER="$abs_top_srcdir/tests/run-python-tests.sh"
RUNNER="$abs_top_srcdir/tests/python-suite-runner.py"
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
# C3 -- CONTROL. AM_PATH_PYTHON sets PYTHON=: when there is no interpreter.
#       The launcher already handles this correctly; it must keep doing so.
#       Needs no interpreter, so it runs first and is checkable everywhere.
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
# C4 -- CONTROL. No tests/python/ at all -> 77, never 0. Also needs nothing.
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

# ---------------------------------------------------------------------------
# From here on an interpreter is needed. Look for one: $PYTHON first (it is
# what the launcher itself will use), then the usual names. AM_PATH_PYTHON
# writes PYTHON=: when it found none, so ":" is not a candidate.
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
    # pytest's default python_files, both patterns -- see the same function in
    # tests/python-suite-runner.py.
    if not (name.endswith(".py") and
            (name.startswith("test_") or name.endswith("_test.py"))):
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

COUNTPY=""
for cand in "${PYTHON:-}" python3 python; do
    [ -n "$cand" ] || continue
    [ "$cand" = ":" ] && continue
    if "$cand" -c "import ast, os, sys" >/dev/null 2>&1; then
        COUNTPY="$cand"
        break
    fi
done

if [ -z "$COUNTPY" ]; then
    echo "check-python-tests-reporting: no usable python interpreter on this" \
         "machine (\$PYTHON='${PYTHON:-}'); C3 and C4 checked, C1/C2/C5 cannot" \
         "be measured."
    if [ "$nb_fail" -ne 0 ]; then
        echo "check-python-tests-reporting: $nb_fail check(s) failed." >&2
        exit 1
    fi
    echo "check-python-tests-reporting: SKIP (exit 77) -- reported in the" \
         "'# SKIP:' column, never as a silent PASS."
    exit 77
fi

declared=`"$COUNTPY" "$COUNTER" "$PYDIR" 2>/dev/null` || declared=""
if [ -z "$declared" ]; then
    fail "the declared-case counter did not run under $COUNTPY on $PYDIR"
    echo "check-python-tests-reporting: $nb_fail check(s) failed." >&2
    exit 1
fi
declared_files=`echo "$declared" | cut -d' ' -f1`
declared_cases=`echo "$declared" | cut -d' ' -f2`

if [ "$declared_files" -lt 1 ] || [ "$declared_cases" -lt 1 ]; then
    fail "the fixture is empty: $declared_files files / $declared_cases cases declared" \
         "in $PYDIR -- this harness would pass vacuously"
fi

echo "check-python-tests-reporting: tests/python/ declares" \
     "$declared_files suites / $declared_cases cases (counted with $COUNTPY)"

# ---------------------------------------------------------------------------
# Helpers: extract the accounting line, and check the exit code agrees with it.
#
# Contract:
#   run-python-tests: suites=<ran>/<declared> cases=<executed>/<declared>
#
# and then:  executed == declared  <=> exit 0
#            executed <  declared  <=> exit 77 (automake SKIP, visible)
#            a red case            <=> exit 1
# ---------------------------------------------------------------------------
acct_line()
{
    sed -n 's/^run-python-tests: \(suites=.*\)$/\1/p' "$1" | tail -n 1
}
acct_field()
{
    # $1 = line, $2 = suites|cases, $3 = 1 (got) or 2 (total)
    echo "$1" | sed -n "s/.*$2=\([0-9]*\)\/\([0-9]*\).*/\\$3/p"
}

# check_accounting <label> <log> <rc> <expected declared files> <expected declared cases>
check_accounting()
{
    _lbl=$1; _log=$2; _rc=$3; _dfiles=$4; _dcases=$5
    _line=`acct_line "$_log"`
    if [ -z "$_line" ]; then
        fail "$_lbl: run-python-tests.sh printed no" \
             "'run-python-tests: suites=N/M cases=N/M' accounting line (exit code" \
             "was $_rc). Without it nothing distinguishes a suite that ran from a" \
             "suite that did not."
        sed 's/^/      /' "$_log" >&2
        return 1
    fi
    _gotc=`acct_field "$_line" cases 1`
    _totc=`acct_field "$_line" cases 2`
    _tots=`acct_field "$_line" suites 2`
    if [ -z "$_gotc" ] || [ -z "$_totc" ]; then
        fail "$_lbl: malformed accounting line: '$_line'"
        return 1
    fi
    if [ "$_tots" -ne "$_dfiles" ] || [ "$_totc" -ne "$_dcases" ]; then
        fail "$_lbl: the launcher declares $_tots/$_totc but the sources declare" \
             "$_dfiles/$_dcases -- the launcher is counting something else"
    fi
    if [ "$_gotc" -eq "$_totc" ]; then
        if [ "$_rc" -ne 0 ] && [ "$_rc" -ne 1 ]; then
            fail "$_lbl: every declared case ran ($_line) but the launcher exited $_rc"
        fi
    else
        if [ "$_rc" -ne 77 ] && [ "$_rc" -ne 1 ]; then
            fail "$_lbl: only $_gotc of $_totc declared cases ran, yet the launcher" \
                 "exited $_rc. An unexecuted case must never be reported as PASS;" \
                 "the honest codes are 77 (SKIP) or 1 (FAIL)."
            sed 's/^/      /' "$_log" >&2
        fi
    fi
    return 0
}

# ---------------------------------------------------------------------------
# C1 -- the launcher must publish an accounting line, and its exit code must
#       agree with it, on the real fixture as it stands on THIS machine.
# ---------------------------------------------------------------------------
c1log="$tmpdir/c1.log"
(
    abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
    "$LAUNCHER"
) >"$c1log" 2>&1
c1rc=$?
check_accounting "C1" "$c1log" "$c1rc" "$declared_files" "$declared_cases"
c1line=`acct_line "$c1log"`
c1_executed=`acct_field "$c1line" cases 1`

# ---------------------------------------------------------------------------
# C2 -- pytest made unimportable. The launcher must STILL publish a consistent
#       accounting line, and cannot claim more executed cases than it did with
#       pytest available: removing a dependency never makes more tests run.
#
# The shadow module makes this deterministic on a machine that has pytest and
# on a machine that has not: both are pushed into the same state.
#
# ⚠️ Deliberately NOT asserted: "the fixture still contains suites that need
# pytest". Porting tests/python/ to stdlib unittest is a legitimate, desirable
# change; it must not make this harness red.
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
exec $COUNTPY "\$@"
STUB_EOF
chmod +x "$stub"

have_stub=no
if ! "$stub" -c "import sys" >/dev/null 2>&1; then
    echo "check-python-tests-reporting: C2/C5b skipped, the shadow interpreter" \
         "is not runnable"
elif "$stub" -c "import pytest" >/dev/null 2>&1; then
    fail "C2: the pytest shadow did not take effect -- the case would be vacuous"
else
    have_stub=yes
    c2log="$tmpdir/c2.log"
    (
        abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
        PYTHON="$stub"; export PYTHON
        "$LAUNCHER"
    ) >"$c2log" 2>&1
    c2rc=$?
    if check_accounting "C2" "$c2log" "$c2rc" "$declared_files" "$declared_cases"; then
        c2line=`acct_line "$c2log"`
        c2_executed=`acct_field "$c2line" cases 1`
        if [ -n "${c1_executed:-}" ] && [ -n "$c2_executed" ] && \
           [ "$c2_executed" -gt "$c1_executed" ]; then
            fail "C2: with pytest hidden the launcher claims $c2_executed executed" \
                 "cases, more than the $c1_executed it claimed with pytest available." \
                 "Removing a dependency cannot make more tests run: the accounting" \
                 "is not measuring execution."
        fi
    fi
fi

# ---------------------------------------------------------------------------
# C5 -- "skipped from the inside" must count as NOT EXECUTED.
#
# This is the second layer of F-PYTEST-1 and the one nothing pinned before:
# installing pytest alone on the build image would NOT make the three
# pytest-only suites run, because pytest.importorskip on fastapi/httpx/
# colorama fires and pytest exits 0. If a skipped case counted as executed,
# the launcher would go straight back to saying PASS on tests that never ran,
# one layer down.
#
# The world is FABRICATED, not observed: a tree of exactly two declared cases,
# one of which cannot run. It therefore says nothing about what tests/python/
# happens to contain today, and cannot rot when the fixture changes.
# ---------------------------------------------------------------------------
c5_tree()
{
    # $1 = tree root; creates <root>/tests/{python,python-suite-runner.py}
    mkdir -p "$1/tests/python" || return 1
    cp "$RUNNER" "$1/tests/python-suite-runner.py" || return 1
    return 0
}

c5_run()
{
    # $1 = label, $2 = tree root, $3 = interpreter, $4 = why it must be 1 of 2
    _lbl=$1; _root=$2; _py=$3; _why=$4
    _d=`"$COUNTPY" "$COUNTER" "$_root/tests/python" 2>/dev/null` || _d=""
    if [ -z "$_d" ]; then
        fail "$_lbl: could not count the fabricated tree"
        return 1
    fi
    _df=`echo "$_d" | cut -d' ' -f1`
    _dc=`echo "$_d" | cut -d' ' -f2`
    if [ "$_dc" -ne 2 ]; then
        fail "$_lbl: the fabricated tree should declare exactly 2 cases, it declares $_dc"
        return 1
    fi
    _log="$tmpdir/`echo "$_lbl" | tr 'A-Z ' 'a-z_'`.log"
    (
        abs_top_srcdir="$_root"; export abs_top_srcdir
        PYTHON="$_py"; export PYTHON
        "$LAUNCHER"
    ) >"$_log" 2>&1
    _rc=$?
    _line=`acct_line "$_log"`
    _got=`acct_field "$_line" cases 1`
    if [ -z "$_line" ] || [ -z "$_got" ]; then
        fail "$_lbl: no accounting line (exit $_rc)"
        sed 's/^/      /' "$_log" >&2
        return 1
    fi
    if [ "$_got" -ne 1 ] || [ "$_rc" -ne 77 ]; then
        fail "$_lbl: $_why So the honest report is 'cases=1/2' and exit 77 (SKIP)." \
             "Got '$_line' and exit $_rc -- one of the two declared cases is being" \
             "paid for by something that is not it, which is F-PYTEST-1 one layer" \
             "down."
        sed 's/^/      /' "$_log" >&2
        return 1
    fi
    return 0
}

# C5a -- stdlib unittest flavour: importable with or without pytest, so it
#        pins BOTH back-ends (pytest collects unittest.TestCase too).
unittree="$tmpdir/c5-unittest"
c5_tree "$unittree" || exit 1
cat > "$unittree/tests/python/test_c5_skipped.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5). Two declared cases,
# one of which can never run. The launcher must report 1 of 2, not 2 of 2.
import unittest


class SkippedIsNotExecuted(unittest.TestCase):
    def test_really_executed(self):
        self.assertTrue(True)

    @unittest.skip("C5: pinned as NOT executed")
    def test_never_executed(self):
        raise AssertionError("this case must never run")
PY_EOF

c5_run "C5a pytest-backend" "$unittree" "$COUNTPY" "Exactly one of the two declared cases can run; the other is skipped from INSIDE the suite, and a skipped case is a case that did NOT execute -- that is the whole point: pytest.importorskip on the missing sidecar deps skips from the inside too, and must not read as PASS."
if [ "$have_stub" = yes ]; then
    c5_run "C5b unittest-backend" "$unittree" "$stub" "Exactly one of the two declared cases can run; the other is skipped from INSIDE the suite, and a skipped case is a case that did NOT execute -- that is the whole point: pytest.importorskip on the missing sidecar deps skips from the inside too, and must not read as PASS."
fi

# C5c -- the parametrization trap, pytest only. One parametrized case expands
#        into 3 junit <testcase> entries; next to it one case is skipped. An
#        accounting that compares COUNTS per file reads 3 >= 2 and reports
#        "every declared case executed" -- which is how the very first version
#        of this runner was fooled. Comparing case NAMES is what closes it.
# C5d -- names, not counts. A suite whose second case runs under a DIFFERENT
#        name than the one declared: the two counts match, the two names do
#        not. An accounting that compares counts per file reports 2 of 2 and
#        exits 0 while a declared case never ran; comparing names reports 1 of
#        2 and exits 77. This is what makes the name comparison itself -- the
#        fix for the parametrize trap -- mutation-covered rather than merely
#        present.
nametree="$tmpdir/c5-names"
c5_tree "$nametree" || exit 1
cat > "$nametree/tests/python/test_c5_names.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5d). Two declared
# cases; the second one is renamed at import time, so exactly two cases run but
# one DECLARED case never does. Counts agree, names do not.
import unittest


class NamesNotCounts(unittest.TestCase):
    def test_declared_and_run(self):
        self.assertTrue(True)

    def test_declared_but_never_run(self):
        raise AssertionError("this case must never run under its own name")


NamesNotCounts.test_running_under_another_name = \
    NamesNotCounts.test_declared_and_run
del NamesNotCounts.test_declared_but_never_run
PY_EOF

c5_run "C5d names-not-counts" "$nametree" "$COUNTPY" "Two cases run and two are declared, but one of them runs under a DIFFERENT name than the one declared, so a declared case never ran. Counts agree; names do not, and names are what is being accounted for."
if [ "$have_stub" = yes ]; then
    c5_run "C5e names-not-counts-unittest" "$nametree" "$stub" "Two cases run and two are declared, but one of them runs under a DIFFERENT name than the one declared, so a declared case never ran. Counts agree; names do not, and names are what is being accounted for."
fi

if "$COUNTPY" -c "import pytest" >/dev/null 2>&1; then
    paramtree="$tmpdir/c5-param"
    c5_tree "$paramtree" || exit 1
    cat > "$paramtree/tests/python/test_c5_param.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5c). Two declared
# cases: one parametrized x3 (so pytest emits 3 junit entries) and one skipped.
# The three instances must NOT pay for the case that never ran.
import pytest


@pytest.mark.parametrize("value", [1, 2, 3])
def test_parametrized(value):
    assert value > 0


@pytest.mark.skip(reason="C5c: pinned as NOT executed")
def test_never_executed():
    raise AssertionError("this case must never run")
PY_EOF
    c5_run "C5c parametrize-trap" "$paramtree" "$COUNTPY" "One declared case is parametrized x3 (pytest emits 3 junit entries for it) and the other is skipped. Three instances of one case are still ONE case: they must not pay for the case that never ran."
else
    echo "check-python-tests-reporting: C5c skipped, pytest is not importable" \
         "under $COUNTPY"
fi

if [ "$nb_fail" -ne 0 ]; then
    echo "" >&2
    echo "check-python-tests-reporting: $nb_fail check(s) failed." >&2
    exit 1
fi

echo "check-python-tests-reporting: run-python-tests.sh reports honestly"
exit 0
