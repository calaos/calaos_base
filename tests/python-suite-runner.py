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
# So this runner does not ask "is pytest there?". It asks "how many cases does
# tests/python/ DECLARE, and how many did I really execute?", and it publishes
# both numbers on one machine-readable line:
#
#     run-python-tests: suites=<ran>/<declared> cases=<executed>/<declared>
#
# Verdict, in this order:
#     any executed case failed or errored          -> 1  (FAIL)
#     a declared case was not executed             -> 77 (SKIP, and the
#                                                     "# SKIP:" column says so)
#     every declared case executed and passed      -> 0  (PASS)
#
# ⚠️ Note that a case skipped from INSIDE the suite (pytest.importorskip on
# fastapi/httpx/colorama, unittest's @skip) counts as NOT executed here. That
# is deliberate: on the reference build image, installing pytest alone would
# NOT make those three suites run -- fastapi, httpx and colorama are missing
# too, importorskip would fire, pytest would exit 0, and the silence would come
# straight back one layer down. Counting the cases, not the dependencies, is
# what closes that door.
# ---------------------------------------------------------------------------

import ast
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


def declared_suites(pydir):
    """{filename: [case names]} read from the SOURCES, never from a runner."""
    suites = {}
    for name in sorted(os.listdir(pydir)):
        if not (name.startswith("test_") and name.endswith(".py")):
            continue
        with open(os.path.join(pydir, name), encoding="utf-8") as handle:
            tree = ast.parse(handle.read(), name)
        cases = []

        def walk(node, prefix):
            for child in node.body:
                if isinstance(child, ast.ClassDef):
                    walk(child, prefix + child.name + ".")
                elif isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    if child.name.startswith("test"):
                        cases.append(prefix + child.name)

        walk(tree, "")
        suites[name] = cases
    return suites


def have_pytest():
    try:
        import pytest  # noqa: F401
    except Exception:
        return False
    return True


def run_with_pytest(pydir):
    """Return (executed_per_file, failed, note). Skipped cases are not executed."""
    executed = {}
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        report = os.path.join(tmp, "junit.xml")
        cmd = [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider",
               "--junit-xml=" + report, pydir]
        proc = subprocess.run(cmd, cwd=pydir)
        if not os.path.exists(report):
            return {}, 0, ("pytest produced no report (exit %d); nothing can be "
                           "accounted for" % proc.returncode)
        root = ET.parse(report).getroot()
        for case in root.iter("testcase"):
            src = case.get("file") or ""
            name = os.path.basename(src)
            if not name:
                # No "file" attribute: fall back on the dotted classname.
                parts = (case.get("classname") or "").split(".")
                mods = [p for p in parts if p.startswith("test_")]
                name = (mods[0] + ".py") if mods else "<unknown>"
            skipped = case.find("skipped") is not None
            bad = (case.find("failure") is not None or case.find("error") is not None)
            if skipped:
                executed.setdefault(name, 0)
                continue
            executed[name] = executed.get(name, 0) + 1
            if bad:
                failed += 1
    return executed, failed, ""


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

    for name in sorted(declared_suites(pydir)):
        module = name[:-3]
        probe = subprocess.run([sys.executable, "-c", "import " + module],
                               cwd=pydir, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if probe.returncode != 0:
            reason = probe.stderr.decode("utf-8", "replace").strip().splitlines()
            executed.setdefault(name, 0)
            notes.append("%s: not importable without pytest (%s)"
                         % (name, reason[-1] if reason else "unknown error"))
            continue

        proc = subprocess.run([sys.executable, "-m", "unittest", "-v", module],
                              cwd=pydir, env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        out = proc.stdout.decode("utf-8", "replace")
        sys.stdout.write(out)
        ran = 0
        skipped = 0
        for line in out.splitlines():
            if line.startswith("Ran ") and " test" in line:
                try:
                    ran = int(line.split()[1])
                except (IndexError, ValueError):
                    ran = 0
            if "skipped=" in line:
                try:
                    skipped = int(line.split("skipped=")[1].split(")")[0].split(",")[0])
                except (IndexError, ValueError):
                    skipped = 0
        executed[name] = max(ran - skipped, 0)
        if proc.returncode != 0:
            failed += 1
            notes.append("%s: unittest reported a failure" % name)
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
        executed, failed, note = run_with_pytest(pydir)
        if note:
            notes.append(note)
    else:
        print("pytest is not importable for %s: running the suites that do not "
              "need it, one module at a time" % sys.executable)
        executed, failed, notes = run_with_unittest(pydir, srcroot)

    ran_cases = 0
    ran_files = 0
    missing = []
    for name, cases in sorted(declared.items()):
        got = executed.get(name, 0)
        want = len(cases)
        ran_cases += min(got, want)
        if got >= want:
            ran_files += 1
        else:
            missing.append((name, got, want))

    print("")
    print("run-python-tests: suites=%d/%d cases=%d/%d"
          % (ran_files, total_files, ran_cases, total_cases))
    for note in notes:
        print("run-python-tests:   %s" % note)
    for name, got, want in missing:
        print("run-python-tests:   NOT RUN: %s (%d of %d declared cases executed)"
              % (name, got, want))

    if failed:
        print("run-python-tests: FAIL, %d red case(s)" % failed)
        return 1
    if missing:
        print("run-python-tests: SKIP (exit 77) -- %d of %d declared cases did not "
              "execute. This suite is NOT green, it is unmeasured; automake will "
              "count it in the '# SKIP:' column."
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
