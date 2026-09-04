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
#   C5  the accounting itself, on FABRICATED source trees. Every case asserts
#                         the SAME five things: the totals the launcher
#                         publishes equal what an independent counter reads
#                         from the sources (BOTH halves, suites= and cases=),
#                         the two numerators are what the fixture makes true,
#                         and the exit code agrees. Each tree is run on both
#                         back-ends where it can be.
#     C5a/C5b  skipped from the inside is NOT executed  -> 1 of 2, exit 77
#     C5c      1 parametrized case (3 junit entries) must not pay for the
#              skipped case next to it                  -> 1 of 2, exit 77
#     C5d/C5e  two cases run, two declared, but one runs under ANOTHER name:
#              counts agree, names do not                -> 1 of 2, exit 77
#     C5f/C5g  ⭐ a NON-FLAT tree: two suites with the SAME BASENAME in
#              different directories. Keyed by basename they merge and the
#              cases of one pay for the cases of the other -> 2 of 3, exit 77
#     C5h/C5i  a "*_test.py" suite is half of pytest's default python_files:
#              it must be DECLARED, or it runs uncounted  -> 1 of 2, exit 77
#     C5j/C5k  an EXPECTED FAILURE is an executed case, on both back-ends,
#              or one xfail freezes make check at SKIP    -> 2 of 2, exit 0
#     C5l      pytest only: xfail(run=False) really is not executed, and a
#              plain xfail next to it is                  -> 2 of 3, exit 77
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

# Every case below (C6 excepted) fabricates a tree whose HONEST verdict is 77,
# and asserts it. CI exports CALAOS_PYTHON_TESTS_REQUIRED=1 for the whole
# `make check`, which would inherit into these sub-invocations and turn all of
# them into 1. C6 re-exports it in its own subshell.
CALAOS_PYTHON_TESTS_REQUIRED=
unset CALAOS_PYTHON_TESTS_REQUIRED

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

import fnmatch

pydir = sys.argv[1]
files = 0
cases = 0
# pytest's default norecursedirs -- see the same list in
# tests/python-suite-runner.py. ⚠️ The walk is RECURSIVE on purpose: a flat
# listing here would disagree with the launcher about what the tree declares,
# and two suites with the same basename in different directories would be
# invisible to this counter.
norec = ("*.egg", ".*", "_darcs", "build", "CVS", "dist", "node_modules",
         "venv", "{arch}", "__pycache__")
for root, dirs, names in os.walk(pydir):
    dirs[:] = sorted(d for d in dirs
                     if not any(fnmatch.fnmatch(d, pat) for pat in norec))
    for name in sorted(names):
        # pytest's default python_files, both patterns -- see the same function
        # in tests/python-suite-runner.py.
        if not (name.endswith(".py") and
                (name.startswith("test_") or name.endswith("_test.py"))):
            continue
        files += 1
        full = os.path.join(root, name)
        tree = ast.parse(open(full, encoding="utf-8").read(), name)

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
    _gots=`acct_field "$_line" suites 1`
    _tots=`acct_field "$_line" suites 2`
    if [ -z "$_gotc" ] || [ -z "$_totc" ] || [ -z "$_gots" ] || [ -z "$_tots" ]; then
        fail "$_lbl: malformed accounting line: '$_line'"
        return 1
    fi
    if [ "$_tots" -ne "$_dfiles" ] || [ "$_totc" -ne "$_dcases" ]; then
        fail "$_lbl: the launcher declares $_tots/$_totc but the sources declare" \
             "$_dfiles/$_dcases -- the launcher is counting something else"
    fi
    # ⭐ The suites= half of the published line is asserted too. Without this,
    # half of what the launcher prints is unmeasured, and a launcher that made
    # its suite counter unconditional would publish the SELF-CONTRADICTORY
    # "suites=6/6 cases=23/42" -- every suite complete, half the cases missing --
    # while every other check here stayed green.
    if [ "$_gotc" -eq "$_totc" ]; then
        if [ "$_gots" -ne "$_tots" ]; then
            fail "$_lbl: '$_line' contradicts itself: every declared case ran," \
                 "yet only $_gots of $_tots suites are counted as complete."
        fi
    else
        if [ "$_gots" -ge "$_tots" ]; then
            fail "$_lbl: '$_line' contradicts itself: $_gots of $_tots suites are" \
                 "counted as complete while only $_gotc of $_totc declared cases" \
                 "ran. A suite with a case that did not execute is NOT complete;" \
                 "the suites= half of this line is not measuring anything."
            sed 's/^/      /' "$_log" >&2
        fi
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

# c5_run <label> <root> <interp> <want files> <want cases> <want ran suites> \
#        <want ran cases> <want rc> <why>
#
# ⚠️ Every C5 case asserts ALL of: the fabricated tree declares what it is meant
# to declare (so the fixture cannot rot into vacuity), the launcher's PUBLISHED
# totals equal that -- BOTH halves, suites= and cases= -- the two numerators are
# exactly what the fixture makes true, and the exit code agrees. Asserting only
# the cases= numerator left the suites= half of the published line, and the
# declaration side itself, pinned by nothing.
c5_run()
{
    _lbl=$1; _root=$2; _py=$3
    _wf=$4; _wc=$5; _wrs=$6; _wrc=$7; _wrcode=$8; _why=$9
    _d=`"$COUNTPY" "$COUNTER" "$_root/tests/python" 2>/dev/null` || _d=""
    if [ -z "$_d" ]; then
        fail "$_lbl: could not count the fabricated tree"
        return 1
    fi
    _df=`echo "$_d" | cut -d' ' -f1`
    _dc=`echo "$_d" | cut -d' ' -f2`
    if [ "$_df" -ne "$_wf" ] || [ "$_dc" -ne "$_wc" ]; then
        fail "$_lbl: the fabricated tree should declare $_wf file(s) / $_wc case(s)," \
             "it declares $_df/$_dc -- the fixture, not the launcher, is wrong"
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
    _tot=`acct_field "$_line" cases 2`
    _gs=`acct_field "$_line" suites 1`
    _ts=`acct_field "$_line" suites 2`
    if [ -z "$_line" ] || [ -z "$_got" ] || [ -z "$_gs" ]; then
        fail "$_lbl: no accounting line (exit $_rc)"
        sed 's/^/      /' "$_log" >&2
        return 1
    fi
    if [ "$_ts" -ne "$_wf" ] || [ "$_tot" -ne "$_wc" ]; then
        fail "$_lbl: the sources declare $_wf file(s) / $_wc case(s) but the launcher" \
             "publishes 'suites=…/$_ts cases=…/$_tot'. The launcher is declaring" \
             "something other than what is on disk, so its numerators cannot mean" \
             "what they say. $_why"
        sed 's/^/      /' "$_log" >&2
        return 1
    fi
    if [ "$_got" -ne "$_wrc" ] || [ "$_gs" -ne "$_wrs" ] || [ "$_rc" -ne "$_wrcode" ]; then
        fail "$_lbl: $_why So the honest report is 'suites=$_wrs/$_wf" \
             "cases=$_wrc/$_wc' and exit $_wrcode." \
             "Got '$_line' and exit $_rc -- what is published is not what ran," \
             "which is F-PYTEST-1 one layer down."
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

c5_run "C5a pytest-backend" "$unittree" "$COUNTPY" 1 2 0 1 77 "Exactly one of the two declared cases can run; the other is skipped from INSIDE the suite, and a skipped case is a case that did NOT execute -- that is the whole point: pytest.importorskip on the missing sidecar deps skips from the inside too, and must not read as PASS."
if [ "$have_stub" = yes ]; then
    c5_run "C5b unittest-backend" "$unittree" "$stub" 1 2 0 1 77 "Exactly one of the two declared cases can run; the other is skipped from INSIDE the suite, and a skipped case is a case that did NOT execute -- that is the whole point: pytest.importorskip on the missing sidecar deps skips from the inside too, and must not read as PASS."
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
#        2 and exits 77.
#
# ⚠️ WHY THIS FIXTURE LOOKS CONTRIVED, AND WHY IT STAYS. Renaming a method at
# import time is not something tests/python/ does or should do. It is here
# because it is the ONLY shape that separates the two accountings, and that
# separation was measured, not imagined: with C5a/C5b/C5c alone, a mutation
# putting back the per-file "executed >= declared" comparison -- the exact
# regression this harness was returned to its author for -- SURVIVED the whole
# campaign. Once the parametrization suffix is stripped upstream, three
# instances of one case collapse to one name, so counts stop lying on their
# own; the only world left where they still lie is this one. A fabricated case
# that kills a real mutant is worth more than a natural case that kills
# nothing. Do not "simplify" it back into a normal-looking suite without first
# re-running the mutation campaign.
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

c5_run "C5d names-not-counts" "$nametree" "$COUNTPY" 1 2 0 1 77 "Two cases run and two are declared, but one of them runs under a DIFFERENT name than the one declared, so a declared case never ran. Counts agree; names do not, and names are what is being accounted for."
if [ "$have_stub" = yes ]; then
    c5_run "C5e names-not-counts-unittest" "$nametree" "$stub" 1 2 0 1 77 "Two cases run and two are declared, but one of them runs under a DIFFERENT name than the one declared, so a declared case never ran. Counts agree; names do not, and names are what is being accounted for."
fi

# ---------------------------------------------------------------------------
# C5f/C5g -- ⭐ THE TREE IS NOT FLAT. Two suites with the SAME BASENAME in two
#            directories. This is the shape that the rest of C5 could not see,
#            because every other fabricated tree here has exactly one directory.
#
# Measured on the real tests/python/ before this case existed: one case of
# test_logger.py marked skip, plus a regress/test_logger.py declaring a case of
# the same dotted name, and the launcher published "suites=6/6 cases=42/42",
# PASS, exit 0 -- no NOT RUN line, no UNDECLARED line. The declaration side read
# one flat directory and the execution side keyed by basename, so the two files
# collapsed into ONE key and the case that ran paid for the case that did not.
# ⚠️ An oracle whose fixtures are all flat cannot fail on a path collision. Do
# not "simplify" this tree back to one directory.
# ---------------------------------------------------------------------------
duptree="$tmpdir/c5-samebasename"
c5_tree "$duptree" || exit 1
mkdir -p "$duptree/tests/python/sub" || exit 1
: > "$duptree/tests/python/sub/__init__.py"
cat > "$duptree/tests/python/test_dup.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5f/C5g). Same BASENAME
# as sub/test_dup.py below, and the case that is SKIPPED here bears the same
# dotted name as a case that RUNS there.
import unittest


class Dup(unittest.TestCase):
    @unittest.skip("C5f: pinned as NOT executed")
    def test_shared_name(self):
        raise AssertionError("this case must never run")

    def test_only_here(self):
        self.assertTrue(True)
PY_EOF
cat > "$duptree/tests/python/sub/test_dup.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5f/C5g). Same basename
# as ../test_dup.py. Both its cases run, and one of them bears the same dotted
# name as the case that is skipped there.
#
# ⚠️ The two files are DELIBERATELY asymmetric -- 2 cases each, and this one
# declares a case (test_only_there) that the other does not. With a symmetric
# pair, merging the two files on a basename key happens to yield the same two
# numbers as the honest accounting, and the collision is invisible. Measured:
# with the first, symmetric version of this fixture, a mutant that keyed the
# executed cases by basename SURVIVED the whole campaign.
import unittest


class Dup(unittest.TestCase):
    def test_shared_name(self):
        self.assertTrue(True)

    def test_only_there(self):
        self.assertTrue(True)
PY_EOF

DUPWHY="Two suites share a BASENAME in two directories. Four cases are declared over the two files; three execute -- the top-level one is skipped from the inside -- so the top-level suite is incomplete and the sub-directory one is complete. Keyed by basename the two files merge into one, the cases of the sub-directory suite pay for the skipped case above it, and the sub-directory suite reads as having run nothing."
c5_run "C5f same-basename pytest-backend" "$duptree" "$COUNTPY" 2 4 1 3 77 "$DUPWHY"
if [ "$have_stub" = yes ]; then
    c5_run "C5g same-basename unittest-backend" "$duptree" "$stub" 2 4 1 3 77 "$DUPWHY"
fi

# C5m -- the SAME tree, but asking pytest for a junit report that carries the
#        file="..." attribute.
#
# ⚠️ Why this exists: pytest's DEFAULT junit family (xunit2, since pytest 6)
# writes no file= at all -- measured on 7.2.1 and on 9.1.1 -- so the launcher
# resolves suites from the classname, and the code that reads file= is never
# reached on a stock run. Two paths, one exercised: this case exercises the
# other one, on the tree where getting it wrong shows.
# ⚠️ It is GUARDED, not assumed: xunit1 is a legacy family and a future pytest
# may drop it. If the probe stops producing a file= attribute the case says so
# and stands down, because a harness that cannot measure has found nothing --
# it must never break `make check` over a collection option going away.
xunit1_ok=no
if "$COUNTPY" -c "import pytest" >/dev/null 2>&1; then
    probe="$tmpdir/xunit1probe"
    mkdir -p "$probe" || exit 1
    printf 'def test_probe():\n    assert True\n' > "$probe/test_probe.py"
    "$COUNTPY" -m pytest -q -p no:cacheprovider -o junit_family=xunit1 \
        --junit-xml="$probe/j.xml" "$probe" >/dev/null 2>&1
    if [ -f "$probe/j.xml" ] && \
       [ -n "`sed -n 's/.*<testcase[^>]* file=\"[^\"]*\".*/found/p' "$probe/j.xml" | head -n 1`" ]; then
        xunit1_ok=yes
    fi
fi
if [ "$xunit1_ok" = yes ]; then
    PYTEST_ADDOPTS="-o junit_family=xunit1"
    export PYTEST_ADDOPTS
    c5_run "C5m same-basename junit-file-attribute" "$duptree" "$COUNTPY" 2 4 1 3 77 "$DUPWHY"
    unset PYTEST_ADDOPTS
else
    echo "check-python-tests-reporting: C5m skipped, no junit report with a" \
         "file= attribute is obtainable under $COUNTPY"
fi

# ---------------------------------------------------------------------------
# C5h/C5i -- "*_test.py" is the other half of pytest's default python_files. A
#            suite named that way is collected and RUN by pytest; if the
#            declaration side does not know the pattern, the suite runs
#            uncounted and its skipped cases are invisible. Nothing pinned this
#            before: the claim "that blind spot is closed" was asserted by no
#            case at all.
# ---------------------------------------------------------------------------
pattree="$tmpdir/c5-patterns"
c5_tree "$pattree" || exit 1
cat > "$pattree/tests/python/test_prefix.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5h/C5i). The "test_*.py"
# half of pytest's default python_files: one case, and it runs.
import unittest


class Prefix(unittest.TestCase):
    def test_runs(self):
        self.assertTrue(True)
PY_EOF
cat > "$pattree/tests/python/suffix_test.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5h/C5i). The "*_test.py"
# half of pytest's default python_files. Its only case is skipped from the
# inside, so a declaration side that does not know this pattern reports
# "everything ran" while this case did not.
import unittest


class Suffix(unittest.TestCase):
    @unittest.skip("C5h: pinned as NOT executed")
    def test_never_executed(self):
        raise AssertionError("this case must never run")
PY_EOF

PATWHY="A '*_test.py' suite is half of pytest's default python_files: it IS collected and run. Two files declare one case each; the case in the '*_test.py' file is skipped from the inside, so it did not execute -- and a launcher that does not declare that pattern would not even know the file exists."
c5_run "C5h suffix-pattern pytest-backend" "$pattree" "$COUNTPY" 2 2 1 1 77 "$PATWHY"
if [ "$have_stub" = yes ]; then
    c5_run "C5i suffix-pattern unittest-backend" "$pattree" "$stub" 2 2 1 1 77 "$PATWHY"
fi

# ---------------------------------------------------------------------------
# C5j/C5k -- ⭐ AN EXPECTED FAILURE IS AN EXECUTED CASE, and the two back-ends
#            must say so alike. Measured before this case existed: the same
#            @unittest.expectedFailure gave "cases=1/2, exit 77" through pytest
#            (whose junit files xfail as <skipped>) and "cases=2/2, exit 0"
#            through unittest (whose addExpectedFailure counts it as run).
# ⚠️ The consequence was not academic: ONE legitimate xfail under tests/python/
# would have pinned `make check` at a PERPETUAL SKIP on every machine that has
# pytest -- i.e. exactly the machines a future ticket is meant to create.
# ---------------------------------------------------------------------------
xfailtree="$tmpdir/c5-xfail"
c5_tree "$xfailtree" || exit 1
cat > "$xfailtree/tests/python/test_c5_xfail.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5j/C5k). Two declared
# cases, BOTH of which execute: the second one runs and fails on purpose, which
# is its expected outcome. Neither back-end may call it "not executed".
import unittest


class ExpectedFailureIsExecuted(unittest.TestCase):
    def test_plain(self):
        self.assertTrue(True)

    @unittest.expectedFailure
    def test_expected_failure(self):
        self.fail("C5j: this failure is the expected outcome, and it RAN")
PY_EOF

XFWHY="Both declared cases execute: an expected failure runs its body and fails, which is its expected outcome -- it is not a case that could not run. Reporting it as unexecuted would freeze make check at a permanent SKIP the day pytest is installed."
c5_run "C5j xfail-is-executed pytest-backend" "$xfailtree" "$COUNTPY" 1 2 1 2 0 "$XFWHY"
if [ "$have_stub" = yes ]; then
    c5_run "C5k xfail-is-executed unittest-backend" "$xfailtree" "$stub" 1 2 1 2 0 "$XFWHY"
fi

if "$COUNTPY" -c "import pytest" >/dev/null 2>&1; then
    paramtree="$tmpdir/c5-param"
    c5_tree "$paramtree" || exit 1
    cat > "$paramtree/tests/python/test_c5_param.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5c). Two declared
# cases: one parametrized x3 (so pytest emits 3 junit entries) and one skipped.
# The three instances must NOT pay for the case that never ran.
import pytest


# ⚠️ EVERY id contains a ']' on purpose. With integer ids the "strip the [...]
# suffix" regex is pinned only in its easiest case; and with even ONE
# well-behaved id among them, an instance still maps back to the declared name
# and pays for all the others. Measured: leaving one plain id let a lazy,
# unanchored variant of the regex survive the campaign.
@pytest.mark.parametrize("value", ["a]b", "]", "c]"])
def test_parametrized(value):
    assert value


@pytest.mark.skip(reason="C5c: pinned as NOT executed")
def test_never_executed():
    raise AssertionError("this case must never run")
PY_EOF
    c5_run "C5c parametrize-trap" "$paramtree" "$COUNTPY" 1 2 0 1 77 "One declared case is parametrized x3 (pytest emits 3 junit entries for it) and the other is skipped. Three instances of one case are still ONE case: they must not pay for the case that never ran."

    # C5l -- the ONE xfail flavour that really does not run. pytest marks it
    #        "[NOTRUN]" in the junit message; a plain xfail beside it does run.
    #        Without this case, "an xfail is executed" would be applied to
    #        xfail(run=False) too, and a case that never ran would read as PASS.
    notruntree="$tmpdir/c5-notrun"
    c5_tree "$notruntree" || exit 1
    cat > "$notruntree/tests/python/test_c5_notrun.py" <<'PY_EOF'
# Fabricated by tests/check-python-tests-reporting.sh (C5l). Three declared
# cases: one plain, one xfail that RUNS and fails as expected, and one
# xfail(run=False) that is never entered at all.
import pytest


def test_plain():
    assert True


@pytest.mark.xfail(reason="C5l: runs, and fails as expected")
def test_xfail_runs():
    assert False


@pytest.mark.xfail(run=False, reason="C5l: pinned as NOT executed")
def test_xfail_not_run():
    raise AssertionError("this case must never run")
PY_EOF
    c5_run "C5l xfail-run-false" "$notruntree" "$COUNTPY" 1 3 0 2 77 "An xfail that RUNS is an executed case; xfail(run=False) is the one flavour that is never entered, and pytest says so by prefixing its junit message with '[NOTRUN]'. Two of the three declared cases execute."
else
    echo "check-python-tests-reporting: C5c/C5l skipped, pytest is not importable" \
         "under $COUNTPY"
fi

# ---------------------------------------------------------------------------
# C6 -- CALAOS_PYTHON_TESTS_REQUIRED=1 must turn "could not execute" from a
# SKIP into a FAIL on EVERY path that produced a 77. C3 and C4 above pin the
# default (still 77); these pin the strict mode CI runs in. 77 is the right
# answer on a developer machine and the wrong one on a machine whose whole job
# is to execute those 42 cases.
# ---------------------------------------------------------------------------
c6_strict()
{
    label=$1
    expected=$2
    shift 2
    c6log="$tmpdir/c6.log"
    (
        CALAOS_PYTHON_TESTS_REQUIRED=1; export CALAOS_PYTHON_TESTS_REQUIRED
        eval "$@"
        "$LAUNCHER"
    ) >"$c6log" 2>&1
    c6rc=$?
    if [ "$c6rc" -ne "$expected" ]; then
        fail "$label: expected exit $expected under" \
             "CALAOS_PYTHON_TESTS_REQUIRED=1, got $c6rc"
        sed -n '1,20p' "$c6log" >&2
    fi
}

c6_strict "C6a (no interpreter)" 1 \
    'abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir; PYTHON=:; export PYTHON'
c6_strict "C6b (no tests/python)" 1 \
    'abs_top_srcdir="$emptytree"; export abs_top_srcdir'
c6_strict "C6c (PYTHON not runnable)" 1 \
    'abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir; PYTHON="$tmpdir/no-such-interpreter"; export PYTHON'

# C6d -- the real tree, whatever this machine has installed. The verdict may
# legitimately be PASS (every case ran) or FAIL (something did not run, or a
# case is red); it must never be SKIP. This is the invariant CI relies on.
c6dlog="$tmpdir/c6d.log"
(
    abs_top_srcdir="$abs_top_srcdir"; export abs_top_srcdir
    CALAOS_PYTHON_TESTS_REQUIRED=1; export CALAOS_PYTHON_TESTS_REQUIRED
    "$LAUNCHER"
) >"$c6dlog" 2>&1
c6drc=$?
if [ "$c6drc" -eq 77 ]; then
    fail "C6d: the launcher returned 77 on the real tree under" \
         "CALAOS_PYTHON_TESTS_REQUIRED=1; strict mode must never skip"
fi

if [ "$nb_fail" -ne 0 ]; then
    echo "" >&2
    echo "check-python-tests-reporting: $nb_fail check(s) failed." >&2
    exit 1
fi

echo "check-python-tests-reporting: run-python-tests.sh reports honestly"
exit 0
