#!/usr/bin/env python3
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
# ---------------------------------------------------------------------------
# F-PYTEST-1 -- run tests/python/ and ACCOUNT for what actually ran.
#
# The rule this file exists to enforce:
#
#     a test that could not be executed must never be reported as PASS.
#
# The previous launcher ran "pytest if available" and otherwise fell back to
#     python3 -m unittest discover -p 'test_t116_*.py'
# The pattern collects 3 of the 6 suites. The other 3 -- 19 of the 42 declared
# cases -- simply vanished, the script exited 0, and automake wrote PASS in the
# .trs. That is worse than a FAIL and worse than a SKIP: an automake SKIP is
# counted in the visible "# SKIP:" column, a PASS on nothing is invisible.
#
# So this runner does not ask "is pytest there?". It asks "WHICH cases does
# tests/python/ DECLARE, and WHICH ones did I really execute?", and it
# publishes the tally on one machine-readable line:
#
#     run-python-tests: suites=<ran>/<declared> cases=<executed>/<declared>
#
# Verdict, in this order:
#     any executed case failed or errored          -> 1  (FAIL)
#     a declared case was not executed             -> 77 (SKIP, and the
#                                                     "# SKIP:" column says so)
#     every declared case executed and passed      -> 0  (PASS)
#
# ⚠️ ACCOUNTING IS BY CASE NAME, NEVER BY COUNT. Comparing counts per file was
# the first version of this runner and it was fooled the same week it was
# written: one @pytest.mark.parametrize case expands into N junit <testcase>
# entries, so a file with "1 parametrized case (x3) + 1 skipped case" reported
# 3 >= 2 and the skipped -- i.e. NOT EXECUTED -- case was absorbed. The runner
# now intersects the SET of declared dotted names with the SET of executed
# ones, so an extra parametrization can never pay for a missing case.
#
# ⚠️ A case skipped from INSIDE the suite (pytest.importorskip on
# fastapi/httpx/colorama, pytest.mark.skip, unittest's @skip) counts as NOT
# executed here. That is deliberate: on the reference build image, installing
# pytest alone would NOT make those three suites run -- fastapi, httpx and
# colorama are missing too, importorskip would fire, pytest would exit 0, and
# the silence would come straight back one layer down. Counting the cases, not
# the dependencies, is what closes that door.
#
# ⚠️ The set of files considered IS pytest's own default `python_files`, i.e.
# both "test_*.py" and "*_test.py". Declaring only the first pattern left a
# blind spot of exactly the same family as the defect above: a "*_test.py"
# suite is collected and run by pytest but was never declared, so it could
# neither be counted nor be missed.
#
# ⚠️ ACCOUNTING IS KEYED BY PATH RELATIVE TO tests/python/, NEVER BY BASENAME,
# and the declaration side WALKS THE TREE instead of listing one flat level.
# The two halves must agree or they collide: with a flat os.listdir on one side
# and os.path.basename on the other, adding tests/python/regress/test_logger.py
# next to tests/python/test_logger.py made the cases of the second pay for the
# cases of the first. Measured on the real tree: one case of test_logger.py
# marked skip, plus that same-basename file in a sub-package, and the launcher
# published "suites=6/6 cases=42/42", PASS, exit 0 -- with no NOT RUN line and
# no UNDECLARED line, because the collision made the two files ONE key. That is
# this file's own defect, committed inside the fix for it.
#
# ⚠️ AN EXPECTED FAILURE IS AN EXECUTED CASE. pytest's junit reports xfail as
# <skipped type="pytest.xfail">, which reads as "not executed" unless the type
# is looked at; unittest's addExpectedFailure has always counted it as run. The
# two back-ends disagreed, and the pytest side was the wrong one: an xfailing
# case DID run, its body raised, and that is the expected outcome. Left
# unfixed, a single legitimate xfail under tests/python/ would have pinned
# `make check` at a PERPETUAL SKIP on every machine that has pytest. The one
# exception is xfail(run=False), whose reason pytest prefixes with "[NOTRUN]":
# that case really is not executed and is counted as such.
#
# ⚠️ KNOWN, DELIBERATE RESIDUE -- read this before trusting a green run.
# When a back-end reports a case this file never declared, the runner prints
#     run-python-tests:   UNDECLARED: <file> ran <names>
# and that is ALL it does: an undeclared case is a NOTE, not a failure, and it
# does NOT change the exit code. The consequence is concrete: someone can add
# a test file under a name neither pattern matches, or a case this module's
# ast walk does not recognise as one, and NOTHING WILL FAIL -- the suite may
# never run and the build stays green. That is the very family of defect this
# file exists to close, left open one notch on purpose: turning it into a hard
# failure would make any collection quirk of a future pytest break `make
# check` for everybody, which is the "franc echec" that
# docs/refactoring/DECISIONS.md rules out. The note is the honest middle: the
# fact is printed, in the same place as the accounting, for a human to read.
# If that trade ever stops being acceptable, the change is one line here --
# and it needs an arbitration, not a patch.
#
# ⚠️ AND THE NOTE IS NOT A COMPLETE SAFETY NET -- say it plainly. It is printed
# only when a back-end REPORTS a file or a case name the declaration side does
# not know. A suite that NOBODY collects -- a file named so that neither
# python_files pattern matches it -- is reported by nobody, so nothing is
# printed at all and the silence is total. The note covers "collected but not
# declared"; it does not cover "collected by no one".
#
# ⚠️ SECOND KNOWN RESIDUE, opposite direction: the ast walk declares the
# test-shaped methods of EVERY class, including a base/mixin class that neither
# back-end collects (pytest collects classes named Test*, unittest collects
# TestCase subclasses; a plain mixin is neither). Its methods are then declared
# and never executed, and the launcher reports cases=0/N and exits 77. That is
# a FALSE SKIP, not a false PASS: it is loud, it names the missing cases on the
# NOT RUN line, and it errs on the side this file exists to defend. Narrowing
# the walk to "collectable-looking" classes would trade this noisy, safe error
# for a silent, unsafe one, so it is left as is -- deliberately.
# ---------------------------------------------------------------------------

import ast
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


# pytest's default `python_files`. Keep the two in sync or the runner grows a
# blind spot: a collected-but-undeclared suite.
def is_suite_file(name):
    return name.endswith(".py") and (name.startswith("test_") or
                                     name.endswith("_test.py"))


# pytest's default `norecursedirs`. Same rule as above: a directory pytest does
# not descend into declares nothing, so mirroring its list is what keeps the
# declaration side and the execution side describing the same tree.
_NORECURSE = ("*.egg", ".*", "_darcs", "build", "CVS", "dist", "node_modules",
              "venv", "{arch}", "__pycache__")


def _recurse_into(dirname):
    import fnmatch
    return not any(fnmatch.fnmatch(dirname, pat) for pat in _NORECURSE)


def declared_suites(pydir):
    """{path relative to pydir: set(dotted case names)} read from the SOURCES,
    never from a runner. 'Dotted' means 'Class.method' for class-based cases and
    plain 'function' for module-level ones -- exactly what both back-ends
    report.

    ⚠️ The key is the RELATIVE PATH, and the walk is recursive. A basename key
    on a tree with two same-named suites in different directories merges them
    into one entry, and the cases of one then pay for the cases of the other.
    """
    suites = {}
    for root, dirs, files in os.walk(pydir):
        dirs[:] = sorted(d for d in dirs if _recurse_into(d))
        for base in sorted(files):
            if not is_suite_file(base):
                continue
            full = os.path.join(root, base)
            name = os.path.relpath(full, pydir).replace(os.sep, "/")
            with open(full, encoding="utf-8") as handle:
                tree = ast.parse(handle.read(), name)
            cases = set()

            def walk(node, prefix, cases=cases):
                for child in node.body:
                    if isinstance(child, ast.ClassDef):
                        walk(child, prefix + child.name + ".", cases)
                    elif isinstance(child, (ast.FunctionDef,
                                            ast.AsyncFunctionDef)):
                        if child.name.startswith("test"):
                            cases.add(prefix + child.name)

            walk(tree, "")
            suites[name] = cases
    return suites


def have_pytest():
    try:
        import pytest  # noqa: F401
    except Exception:
        return False
    return True


_PARAM_SUFFIX = re.compile(r"\[.*\]$", re.S)


def _is_expected_failure(skipped):
    """True when this junit <skipped> is an xfail, i.e. a case that DID run.

    pytest files xfail under <skipped type="pytest.xfail">; a real skip is
    type="pytest.skip" (or has no type at all on very old versions). The single
    xfail flavour that really does not run is xfail(run=False), whose message
    pytest prefixes with "[NOTRUN]".
    """
    if (skipped.get("type") or "") != "pytest.xfail":
        return False
    message = skipped.get("message") or ""
    return not message.lstrip().startswith("[NOTRUN]")


def rel_to_pydir(raw, pydir):
    """junit's file="..." turned into a path relative to pydir.

    ⚠️ file= is written only by the xunit1/legacy junit families; the default
    (xunit2) omits it, so on a stock pytest this function is never reached and
    src_from_classname does the work. It is kept because a report that DOES
    carry file= is exact, and pinned by a dedicated oracle case that asks for
    junit_family=xunit1 explicitly.

    pytest writes it relative to its rootdir, which is pydir itself (we pass
    --rootdir) or, if a future pytest ignores that, one of pydir's ancestors.
    Both are resolved here. ⚠️ The fallback is the RAW string, never the
    basename: an unresolvable path must land on a key that matches nothing and
    surface as UNDECLARED, not silently merge with a declared suite.
    """
    if not raw:
        return ""
    raw = raw.replace("\\", "/")
    if os.path.isabs(raw):
        cands = [raw]
    else:
        cands = []
        base = pydir
        while True:
            cands.append(os.path.join(base, raw))
            parent = os.path.dirname(base)
            if parent == base:
                break
            base = parent
    for cand in cands:
        cand = os.path.normpath(cand)
        if not cand.startswith(pydir + os.sep):
            continue
        if os.path.exists(cand):
            return os.path.relpath(cand, pydir).replace(os.sep, "/")
    return raw


def src_from_classname(classname, pydir, declared):
    """The path a junit classname points at, or "" when it is not certain.

    ⚠️ THIS IS THE PATH THAT ACTUALLY RUNS. pytest's default junit family
    (xunit2 since pytest 6) does NOT write file= at all -- measured on 7.2.1 and
    on 9.1.1 -- so the classname is all there is, and the previous version of
    this function took its FIRST module-looking component and appended ".py".
    That is where the same-basename collision really came from.

    Two steps, in order:
      1. the dotted classname IS a path when the directory is a package: the
         longest prefix that exists on disk wins, so "sub.test_dup.Dup" lands
         on "sub/test_dup.py" and not on "test_dup.py";
      2. otherwise (a sub-directory with no __init__.py: pytest names the module
         after the file alone) fall back to the basename -- but ONLY if exactly
         one declared suite bears it. Two candidates means the answer is not
         known, and an unknown answer must stay unknown: returning either one
         would be the collision again, silently.
    """
    parts = [p for p in (classname or "").split(".") if p]
    for i in range(len(parts), 0, -1):
        if not is_suite_file(parts[i - 1] + ".py"):
            continue
        rel = "/".join(parts[:i]) + ".py"
        if os.path.exists(os.path.join(pydir, rel)):
            return rel
    for part in parts:
        base = part + ".py"
        if not is_suite_file(base):
            continue
        hits = [d for d in declared if os.path.basename(d) == base]
        if len(hits) == 1:
            return hits[0]
        if len(hits) > 1:
            return ""
    return ""


def junit_case_id(case, stem):
    """(file, dotted name) for one junit <testcase>.

    pytest writes classname="<module>[.<Class>...]" and name="<method>", with
    the parametrization appended to name between brackets. The brackets are
    stripped: every instance of a parametrized case maps back to the ONE case
    the sources declare, so N instances can never stand in for N cases.
    """
    name = _PARAM_SUFFIX.sub("", case.get("name") or "")
    parts = [p for p in (case.get("classname") or "").split(".") if p]
    if stem and stem in parts:
        # Everything up to and including the module component is package
        # noise; what is left is the Class[.Class...] path, or nothing.
        parts = parts[parts.index(stem) + 1:]
    return ".".join(parts + [name]) if name else ""


def run_with_pytest(pydir, declared):
    """(executed_names_per_file, failed, note). Skipped cases are NOT executed."""
    executed = {}
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, "junit.xml")
        # --rootdir pins what junit's file="..." is relative to; rel_to_pydir
        # copes anyway, but pinning it makes the common case exact.
        cmd = [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
               "--rootdir", pydir, "--junit-xml=" + report, pydir]
        proc = subprocess.run(cmd, cwd=pydir)
        if not os.path.exists(report):
            return {}, 0, ("pytest produced no report (exit %d); nothing can be "
                           "accounted for" % proc.returncode)
        root = ET.parse(report).getroot()
        for case in root.iter("testcase"):
            # file= comes first because it is exact when it is there at all
            # (junit_family=xunit1/legacy); with the default family it is
            # absent and the classname is what resolves.
            src = (rel_to_pydir(case.get("file"), pydir) or
                   src_from_classname(case.get("classname"), pydir, declared) or
                   "<unknown>")
            base = os.path.basename(src)
            stem = base[:-3] if base.endswith(".py") else base
            ident = junit_case_id(case, stem)
            executed.setdefault(src, set())
            skipped = case.find("skipped")
            if skipped is not None and not _is_expected_failure(skipped):
                # Skipped from the inside: collected, never executed.
                continue
            if not ident:
                continue
            executed[src].add(ident)
            if (case.find("failure") is not None or
                    case.find("error") is not None):
                failed += 1
    return executed, failed, ""


# Driver used when pytest is absent. It reports the ID of every case it really
# executed instead of letting the caller re-parse unittest's prose -- the
# prose has no stable format across Python versions, and counting "Ran N" then
# subtracting "skipped=N" is exactly the kind of arithmetic that let a skipped
# case pass for an executed one in the first version of this runner.
_UNITTEST_DRIVER = r'''
import json, os, sys, unittest

# Running a script by path puts the SCRIPT's directory on sys.path, not the
# working directory the way "python -m unittest" does. The suites live in the
# working directory, so put it back.
sys.path.insert(0, os.getcwd())

module = sys.argv[1]
executed, skipped, bad = [], [], []

def ident(test):
    tid = getattr(test, "id", lambda: str(test))()
    tid = tid.split(" ")[0]
    parts = tid.split(".")
    # The module may be dotted ("regress.test_logger"): strip ALL of its
    # components, or a nested suite reports "pkg.mod.Class.method" against a
    # declaration that says "Class.method" and every case reads as missing.
    mparts = module.split(".")
    if parts[:len(mparts)] == mparts:
        parts = parts[len(mparts):]
    return ".".join(parts)

class Result(unittest.TextTestResult):
    def addSuccess(self, test):
        unittest.TextTestResult.addSuccess(self, test); executed.append(ident(test))
    def addFailure(self, test, err):
        unittest.TextTestResult.addFailure(self, test, err)
        executed.append(ident(test)); bad.append(ident(test))
    def addError(self, test, err):
        unittest.TextTestResult.addError(self, test, err)
        executed.append(ident(test)); bad.append(ident(test))
    def addSkip(self, test, reason):
        unittest.TextTestResult.addSkip(self, test, reason); skipped.append(ident(test))
    def addExpectedFailure(self, test, err):
        unittest.TextTestResult.addExpectedFailure(self, test, err)
        executed.append(ident(test))
    def addUnexpectedSuccess(self, test):
        unittest.TextTestResult.addUnexpectedSuccess(self, test)
        executed.append(ident(test)); bad.append(ident(test))

suite = unittest.TestLoader().loadTestsFromName(module)
runner = unittest.TextTestRunner(verbosity=2, resultclass=Result)
res = runner.run(suite)
sys.stdout.write("PYSUITE-JSON " + json.dumps({
    "executed": sorted(set(executed)),
    "skipped": sorted(set(skipped)),
    "bad": sorted(set(bad)),
}) + "\n")
'''


def run_with_unittest(pydir, srcroot):
    """No pytest: run every suite that imports without it, one module at a time.

    No filename pattern is involved. The old 'test_t116_*.py' glob was the
    mechanism of the defect: it decided in advance which suites counted.
    """
    executed = {}
    failed = 0
    notes = []

    env = dict(os.environ)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    # Same two paths tests/python/conftest.py adds for pytest. conftest.py is a
    # pytest concept, so without pytest they must be provided here or the
    # suites that import calaos_mcp / calaos_extern_proc could not run at all.
    extra = [os.path.join(srcroot, "src", "bin", "calaos_mcp", "python"),
             os.path.join(srcroot, "src", "lib", "calaos-python")]
    env["PYTHONPATH"] = os.pathsep.join(extra + ([env["PYTHONPATH"]]
                                                 if env.get("PYTHONPATH") else []))

    with tempfile.TemporaryDirectory() as tmp:
        driver = os.path.join(tmp, "unittest_driver.py")
        with open(driver, "w", encoding="utf-8") as handle:
            handle.write(_UNITTEST_DRIVER)

        for name in sorted(declared_suites(pydir)):
            # name is a path relative to pydir; the module is its dotted form.
            # A sub-directory without __init__.py simply fails to import and is
            # reported as such -- which is the truth: it did not run.
            module = name[:-3].replace("/", ".")
            executed.setdefault(name, set())
            probe = subprocess.run([sys.executable, "-c", "import " + module],
                                   cwd=pydir, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if probe.returncode != 0:
                reason = probe.stderr.decode("utf-8", "replace").strip().splitlines()
                notes.append("%s: not importable without pytest as '%s' (%s)"
                             % (name, module,
                                reason[-1] if reason else "unknown error"))
                continue

            proc = subprocess.run([sys.executable, driver, module],
                                  cwd=pydir, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            out = proc.stdout.decode("utf-8", "replace")
            payload = None
            for line in out.splitlines():
                if line.startswith("PYSUITE-JSON "):
                    try:
                        payload = json.loads(line[len("PYSUITE-JSON "):])
                    except ValueError:
                        payload = None
                else:
                    sys.stdout.write(line + "\n")
            if payload is None:
                notes.append("%s: the unittest driver produced no report (exit %d)"
                             % (name, proc.returncode))
                continue
            executed[name] = set(payload.get("executed", []))
            if payload.get("bad"):
                failed += len(payload["bad"])
                notes.append("%s: unittest reported %d red case(s)"
                             % (name, len(payload["bad"])))
    return executed, failed, notes


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: python-suite-runner.py <tests/python dir>\n")
        return 1
    pydir = os.path.abspath(argv[1])
    srcroot = os.path.abspath(os.path.join(pydir, "..", ".."))

    declared = declared_suites(pydir)
    total_files = len(declared)
    total_cases = sum(len(v) for v in declared.values())

    if total_files == 0 or total_cases == 0:
        print("SKIP: %s declares no test case" % pydir)
        return 77

    notes = []
    if have_pytest():
        print("Running python suites with pytest (%s)" % sys.executable)
        executed, failed, note = run_with_pytest(pydir, declared)
        if note:
            notes.append(note)
    else:
        print("pytest is not importable for %s: running the suites that do not "
              "need it, one module at a time" % sys.executable)
        executed, failed, notes = run_with_unittest(pydir, srcroot)

    ran_cases = 0
    ran_files = 0
    missing = []
    unexpected = []
    for name, cases in sorted(declared.items()):
        got = executed.get(name, set())
        hit = cases & got
        ran_cases += len(hit)
        if hit == cases:
            ran_files += 1
        else:
            missing.append((name, sorted(cases - got)))
        strays = sorted(got - cases)
        if strays:
            unexpected.append((name, strays))
    # A back-end that reports a case from a file we never declared means the
    # declaration side has a blind spot; say so rather than silently ignore it.
    for name in sorted(set(executed) - set(declared)):
        if executed[name]:
            unexpected.append((name, sorted(executed[name])))

    print("")
    print("run-python-tests: suites=%d/%d cases=%d/%d"
          % (ran_files, total_files, ran_cases, total_cases))
    for note in notes:
        print("run-python-tests:   %s" % note)
    for name, names in missing:
        print("run-python-tests:   NOT RUN: %s (%d of %d declared cases executed; "
              "missing: %s)"
              % (name, len(declared[name]) - len(names), len(declared[name]),
                 ", ".join(names)))
    for name, names in unexpected:
        # NOTE only, never a failure: see the "KNOWN, DELIBERATE RESIDUE" note
        # at the top of this file. This line does not affect the exit code.
        print("run-python-tests:   UNDECLARED (note, does not fail the suite): "
              "%s ran %s -- the declaration side did not know about it"
              % (name, ", ".join(names)))

    if failed:
        print("run-python-tests: FAIL, %d red case(s)" % failed)
        return 1
    if missing:
        print("run-python-tests: SKIP -- %d of %d declared cases did not execute. "
              "This suite is NOT green, it is unmeasured; automake will count it "
              "in the '# SKIP:' column."
              % (total_cases - ran_cases, total_cases))
        print("run-python-tests: install the sidecar test dependencies to close "
              "the gap, e.g.")
        print("run-python-tests:   pip3 install pytest fastapi httpx colorama "
              "--break-system-packages")
        return 77
    print("run-python-tests: PASS, every declared case executed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
