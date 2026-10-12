#!/usr/bin/env python3
"""Self-test of scripts/check-diff-coverage.py.

The gate is only worth something if it can be seen failing, so every case
feeds it a synthetic lcov tracefile and diff. TestCounterMutations then breaks
the script itself, one defect at a time, and requires this suite to turn red.

CALAOS_DIFFCOV_SCRIPT points the suite at another copy of the script (that is
how the mutants are judged); CALAOS_DIFFCOV_NO_MUTANTS=1 skips the mutation
class when the suite is already running against a mutant.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REAL_SCRIPT = os.path.join(os.path.dirname(HERE), "scripts", "check-diff-coverage.py")
SCRIPT = os.environ.get("CALAOS_DIFFCOV_SCRIPT", REAL_SCRIPT)


def source(n, **overrides):
    return "\n".join(overrides.get(str(i), "stmt%d;" % i) for i in range(1, n + 1)) + "\n"


def diff_for(path, *hunks, old=None):
    out = ["diff --git a/%s b/%s" % (old or path, path)]
    if old:
        out += ["similarity index 80%", "rename from " + old, "rename to " + path]
    if hunks:
        out += ["--- a/%s" % (old or path), "+++ b/%s" % path]
    for oc, os_, nc, ns in hunks:
        out.append("@@ -%d,%d +%d%s @@" % (os_, oc, nc, "" if ns is None else ",%d" % ns))
    return "\n".join(out) + "\n"


def info_for(**records):
    out = []
    for path, hits in records.items():
        out.append("TN:\nSF:%s" % path.replace("__", "/").replace("_dot_", "."))
        out += ["DA:%d,%d" % (n, c) for n, c in sorted(hits.items())]
        out.append("end_of_record")
    return "\n".join(out) + "\n"


class GateCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="diffcov-")
        self.addCleanup(shutil.rmtree, self.root, True)

    def put(self, path, text):
        full = os.path.join(self.root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w") as fh:
            fh.write(text)

    def gate(self, info, diff, *extra):
        with open(os.path.join(self.root, "cov.info"), "w") as fh:
            fh.write(info)
        with open(os.path.join(self.root, "pr.diff"), "w") as fh:
            fh.write(diff)
        done = subprocess.run(
            [sys.executable, SCRIPT, "--info", os.path.join(self.root, "cov.info"),
             "--diff", os.path.join(self.root, "pr.diff"), "--root", self.root, *extra],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        return done.returncode, done.stdout, done.stderr

    def tracefile(self, hits, path="src/a.cpp"):
        return "TN:\nSF:%s\n%send_of_record\n" % (
            os.path.join(self.root, path),
            "".join("DA:%d,%d\n" % (n, c) for n, c in sorted(hits.items())))


class TestVerdict(GateCase):
    def test_covered_added_lines_pass(self):
        self.put("src/a.cpp", source(10))
        rc, out, _ = self.gate(self.tracefile({3: 4, 4: 1}),
                               diff_for("src/a.cpp", (0, 3, 2, 3)))
        self.assertEqual(rc, 0, out)
        self.assertIn("3 added lines", out)
        self.assertIn("2 covered", out)

    def test_uncovered_added_line_fails_and_is_listed(self):
        self.put("src/a.cpp", source(10, **{"4": "launch_missiles();"}))
        rc, out, _ = self.gate(self.tracefile({3: 1, 4: 0, 5: 1}),
                               diff_for("src/a.cpp", (0, 3, 3, 3)))
        self.assertEqual(rc, 1)
        self.assertIn("src/a.cpp:4: launch_missiles();", out)
        self.assertNotIn("src/a.cpp:3:", out)

    def test_line_without_record_is_not_executable(self):
        self.put("src/a.cpp", source(10, **{"3": "// a comment", "4": "int declared;"}))
        rc, out, _ = self.gate(self.tracefile({5: 1}),
                               diff_for("src/a.cpp", (0, 3, 3, 3)))
        self.assertEqual(rc, 0, out)
        self.assertIn("2 not executable", out)

    def test_hit_counts_of_one_line_are_summed(self):
        self.put("src/a.h", source(5))
        info = ("SF:%s/src/a.h\nDA:2,7\nend_of_record\n"
                "SF:%s/src/a.h\nDA:2,0\nend_of_record\n" % (self.root, self.root))
        rc, out, _ = self.gate(info, diff_for("src/a.h", (0, 1, 2, None)))
        self.assertEqual(rc, 0, out)

    def test_relative_tracefile_paths_are_accepted(self):
        self.put("src/a.cpp", source(5))
        rc, _, _ = self.gate("SF:src/a.cpp\nDA:2,0\nend_of_record\n",
                             diff_for("src/a.cpp", (0, 1, 2, None)))
        self.assertEqual(rc, 1)

    def test_single_line_hunk_without_count(self):
        self.put("src/a.cpp", source(5))
        rc, out, _ = self.gate(self.tracefile({2: 0}), diff_for("src/a.cpp", (1, 1, 2, None)))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/a.cpp:2:", out)

    def test_last_line_of_a_hunk_is_judged(self):
        self.put("src/a.cpp", source(10))
        rc, out, _ = self.gate(self.tracefile({3: 1, 4: 1, 5: 0}),
                               diff_for("src/a.cpp", (0, 3, 3, 3)))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/a.cpp:5:", out)

    def test_every_hunk_of_a_file_is_judged(self):
        self.put("src/a.cpp", source(40))
        rc, out, _ = self.gate(self.tracefile({3: 1, 30: 0}),
                               diff_for("src/a.cpp", (0, 1, 3, 1), (0, 1, 30, 1)))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/a.cpp:30:", out)

    def test_every_file_of_the_diff_is_judged(self):
        self.put("src/a.cpp", source(5))
        self.put("src/b.cpp", source(5))
        info = self.tracefile({2: 1}, "src/a.cpp") + self.tracefile({2: 0}, "src/b.cpp")
        rc, out, _ = self.gate(info, diff_for("src/a.cpp", (0, 1, 2, 1))
                               + diff_for("src/b.cpp", (0, 1, 2, 1)))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/b.cpp:2:", out)

    def test_report_only_lists_but_does_not_fail(self):
        self.put("src/a.cpp", source(5))
        rc, out, _ = self.gate(self.tracefile({2: 0}), diff_for("src/a.cpp", (0, 1, 2, 1)),
                               "--report-only")
        self.assertEqual(rc, 0)
        self.assertIn("src/a.cpp:2:", out)


class TestDiffShapes(GateCase):
    def test_deleted_lines_are_not_added_lines(self):
        self.put("src/a.cpp", source(20))
        diff = diff_for("src/a.cpp", (3, 11, 11, 0))
        rc, out, _ = self.gate(self.tracefile({11: 0, 12: 0}), diff)
        self.assertEqual(rc, 0, out)

    def test_deleted_file_is_ignored(self):
        diff = ("diff --git a/src/gone.cpp b/src/gone.cpp\ndeleted file mode 100644\n"
                "--- a/src/gone.cpp\n+++ /dev/null\n@@ -1,4 +0,0 @@\n")
        rc, out, _ = self.gate(self.tracefile({1: 0}, "src/gone.cpp"), diff)
        self.assertEqual(rc, 0, out)

    def test_renamed_file_is_judged_under_its_new_name(self):
        self.put("src/new.cpp", source(10, **{"6": "fresh();"}))
        info = self.tracefile({6: 0, 2: 1}, "src/new.cpp") + self.tracefile({6: 1}, "src/old.cpp")
        rc, out, _ = self.gate(info, diff_for("src/new.cpp", (1, 6, 6, 1), old="src/old.cpp"))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/new.cpp:6: fresh();", out)
        self.assertNotIn("src/old.cpp", out)

    def test_pure_rename_adds_no_line(self):
        diff = diff_for("src/new.cpp", old="src/old.cpp")
        rc, out, _ = self.gate(self.tracefile({1: 0}, "src/new.cpp"), diff)
        self.assertEqual(rc, 0, out)

    def test_files_outside_src_are_out_of_scope(self):
        self.put("tests/t.cpp", source(5))
        self.put("src/script.py", source(5))
        self.put("scripts/x.cpp", source(5))
        info = (self.tracefile({2: 0}, "tests/t.cpp") + self.tracefile({2: 0}, "src/script.py")
                + self.tracefile({2: 0}, "scripts/x.cpp") + self.tracefile({1: 1}))
        diff = "".join(diff_for(p, (0, 1, 2, 1))
                       for p in ("tests/t.cpp", "src/script.py", "scripts/x.cpp"))
        rc, out, _ = self.gate(info, diff)
        self.assertEqual(rc, 0, out)
        self.assertIn("3 added lines outside the gate", out)

    def test_vendored_trees_are_out_of_scope(self):
        self.put("src/lib/uvw/x.hpp", source(5))
        rc, out, _ = self.gate(self.tracefile({2: 0}), diff_for("src/lib/uvw/x.hpp", (0, 1, 2, 1)))
        self.assertEqual(rc, 0, out)


class TestExclusions(GateCase):
    def test_excluded_line_passes_and_is_counted(self):
        self.put("src/a.cpp", source(10, **{"3": "abort(); // LCOV_EXCL_LINE unreachable guard"}))
        rc, out, _ = self.gate(self.tracefile({3: 0}), diff_for("src/a.cpp", (0, 1, 3, 1)))
        self.assertEqual(rc, 0, out)
        self.assertIn("Excluded lines added by this diff: 1", out)
        self.assertIn("src/a.cpp: 3", out)

    def test_excluded_block_is_counted_and_ends_at_stop(self):
        text = source(12, **{"3": "// LCOV_EXCL_START needs real hardware",
                             "6": "// LCOV_EXCL_STOP"})
        self.put("src/a.cpp", text)
        rc, out, _ = self.gate(self.tracefile({4: 0, 5: 0, 7: 0}),
                               diff_for("src/a.cpp", (0, 6, 3, 6)))
        self.assertEqual(rc, 1, out)
        self.assertIn("Excluded lines added by this diff: 4", out)
        self.assertIn("src/a.cpp: 3-6", out)
        self.assertIn("src/a.cpp:7:", out)
        self.assertNotIn("src/a.cpp:4:", out)

    def test_exclusion_without_reason_fails(self):
        for marker in ("// LCOV_EXCL_LINE", "/* LCOV_EXCL_LINE */", "// LCOV_EXCL_START"):
            self.put("src/a.cpp", source(10, **{"3": "abort(); " + marker}))
            rc, out, _ = self.gate(self.tracefile({3: 0}), diff_for("src/a.cpp", (0, 1, 3, 1)))
            self.assertEqual(rc, 1, marker + "\n" + out)
            self.assertIn("without a reason", out)

    def test_unclosed_block_fails(self):
        self.put("src/a.cpp", source(10, **{"3": "// LCOV_EXCL_START forever"}))
        rc, out, _ = self.gate(self.tracefile({3: 0}), diff_for("src/a.cpp", (0, 1, 3, 1)))
        self.assertEqual(rc, 1, out)
        self.assertIn("never closed", out)

    def test_old_exclusions_are_not_charged_to_the_diff(self):
        self.put("src/a.cpp", source(10, **{"7": "old(); // LCOV_EXCL_LINE"}))
        rc, out, _ = self.gate(self.tracefile({3: 1}), diff_for("src/a.cpp", (0, 1, 3, 1)))
        self.assertEqual(rc, 0, out)
        self.assertIn("Excluded lines added by this diff: 0", out)


class TestMissingData(GateCase):
    def test_empty_tracefile_is_an_input_error(self):
        self.put("src/a.cpp", source(5))
        rc, _, err = self.gate("TN:\n", diff_for("src/a.cpp", (0, 1, 2, 1)))
        self.assertEqual(rc, 2)
        self.assertIn("no SF record", err)

    def test_missing_source_is_an_input_error(self):
        rc, _, err = self.gate(self.tracefile({2: 1}), diff_for("src/a.cpp", (0, 1, 2, 1)))
        self.assertEqual(rc, 2)
        self.assertIn("cannot read src/a.cpp", err)

    def test_file_never_compiled_fails(self):
        self.put("src/other.cpp", source(5, **{"2": "run_it();"}))
        rc, out, _ = self.gate(self.tracefile({1: 1}), diff_for("src/other.cpp", (0, 1, 2, 1)))
        self.assertEqual(rc, 1, out)
        self.assertIn("src/other.cpp:2: run_it();", out)

    def test_comment_only_change_in_uncompiled_file_passes(self):
        self.put("src/other.cpp", source(5, **{"2": "  // why", "3": "", "4": " * more"}))
        rc, out, _ = self.gate(self.tracefile({1: 1}), diff_for("src/other.cpp", (0, 3, 2, 3)))
        self.assertEqual(rc, 0, out)


class TestAgainstRealGit(GateCase):
    def git(self, *args):
        subprocess.run(["git", "-C", self.root, "-c", "user.name=t", "-c", "user.email=t@t",
                        *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_base_ref_diff_with_a_rename_and_an_edit(self):
        if shutil.which("git") is None:
            self.skipTest("no git")
        body = "".join("int line%d = %d;\n" % (i, i) for i in range(30))
        self.put("src/old.cpp", body)
        self.git("init", "-q", "-b", "base")
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "base")
        self.git("checkout", "-q", "-b", "topic")
        self.git("mv", "src/old.cpp", "src/new.cpp")
        self.put("src/new.cpp", body.replace("line20 = 20", "line20 = 99"))
        self.git("commit", "-q", "-am", "rename and edit")
        with open(os.path.join(self.root, "cov.info"), "w") as fh:
            fh.write(self.tracefile({21: 0, 5: 1}, "src/new.cpp"))
        done = subprocess.run(
            [sys.executable, SCRIPT, "--info", os.path.join(self.root, "cov.info"),
             "--base", "base", "--root", self.root],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(done.returncode, 1, done.stdout + done.stderr)
        self.assertIn("src/new.cpp:21: int line20 = 99;", done.stdout)
        self.assertIn("1 added lines", done.stdout)

    def test_unknown_base_is_an_input_error(self):
        if shutil.which("git") is None:
            self.skipTest("no git")
        self.git("init", "-q")
        with open(os.path.join(self.root, "cov.info"), "w") as fh:
            fh.write(self.tracefile({1: 1}))
        done = subprocess.run(
            [sys.executable, SCRIPT, "--info", os.path.join(self.root, "cov.info"),
             "--base", "nope", "--root", self.root],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(done.returncode, 2)


# (name, [(old, new)], test that must go red). `old` must occur exactly once so
# a refactor of the script cannot leave a mutant silently inert.
MUTANTS = [
    ("exit-always-0", [("    return 1 if failed else 0\n", "    return 0\n")],
     "test_uncovered_added_line_fails_and_is_listed"),
    ("only-first-hunk",
     [("added[cur].update(range(start, start + count))",
       "added[cur] = added[cur] or set(range(start, start + count))")],
     "test_every_hunk_of_a_file_is_judged"),
    ("hunk-loses-last-line",
     [("range(start, start + count)", "range(start, start + count - 1)")],
     "test_last_line_of_a_hunk_is_judged"),
    ("deleted-hunk-counts-one-line",
     [("count = 1 if m.group(2) is None else int(m.group(2))", "count = 1")],
     "test_deleted_lines_are_not_added_lines"),
    ("new-path-keeps-b-prefix",
     [('cur = target[2:] if target.startswith("b/") else target', "cur = target")],
     "test_renamed_file_is_judged_under_its_new_name"),
    ("src-filter-removed",
     [('(not path.startswith("src/") or not path.endswith(CODE_EXT)',
       "(not path.endswith(CODE_EXT)")],
     "test_files_outside_src_are_out_of_scope"),
    ("exclusions-ignored", [("            if n in excluded:", "            if False:")],
     "test_excluded_line_passes_and_is_counted"),
    ("stop-marker-leaks",
     [("            excluded.add(no)\n            open_at = None\n", "            excluded.add(no)\n")],
     "test_excluded_block_is_counted_and_ends_at_stop"),
    ("reason-not-required", [("if no in added and kind != \"STOP\" and not reason:", "if False:")],
     "test_exclusion_without_reason_fails"),
    ("excluded-count-hidden",
     [('w("Excluded lines added by this diff: %d\\n" % total_excl)',
       'w("Excluded lines added by this diff: %d\\n" % 0)')],
     "test_excluded_line_passes_and_is_counted"),
    ("zero-hits-count-as-covered", [("elif data[n] > 0:", "elif data[n] >= 0:")],
     "test_uncovered_added_line_fails_and_is_listed"),
    ("non-executable-lines-counted",
     [('            elif n not in data:\n                res["non_exec"] += 1\n', ""),
      ("elif data[n] > 0:", "elif data.get(n, 0) > 0:")],
     "test_line_without_record_is_not_executable"),
    ("duplicate-records-overwrite",
     [("cur[line] = cur.get(line, 0) + hits", "cur[line] = hits")],
     "test_hit_counts_of_one_line_are_summed"),
    ("empty-tracefile-accepted", [("if not coverage:", "if False:")],
     "test_empty_tracefile_is_an_input_error"),
    ("uncompiled-file-passes",
     [('res["nodata"].append((path, n, src[n - 1].strip()))', 'res["non_exec"] += 1')],
     "test_file_never_compiled_fails"),
    ("report-only-always", [("if failed and args.report_only:", "if failed:")],
     "test_uncovered_added_line_fails_and_is_listed"),
]


def red_tests(script):
    env = dict(os.environ, CALAOS_DIFFCOV_SCRIPT=script, CALAOS_DIFFCOV_NO_MUTANTS="1",
               PYTHONDONTWRITEBYTECODE="1")
    done = subprocess.run([sys.executable, os.path.abspath(__file__), "-v"], env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          universal_newlines=True)
    red = sorted(set(re.findall(r"^(test\w+) \(.*\) \.\.\. (?:FAIL|ERROR)$", done.stdout, re.M)))
    ran = len(re.findall(r"^test\w+ \(.*\) \.\.\. ", done.stdout, re.M))
    return done.returncode, red, ran


@unittest.skipIf(os.environ.get("CALAOS_DIFFCOV_NO_MUTANTS"), "judging a mutant")
class TestCounterMutations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="diffcov-mut-")
        with open(REAL_SCRIPT) as fh:
            cls.text = fh.read()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, True)

    def mutate(self, name, edits):
        text = self.text
        for old, new in edits:
            self.assertEqual(text.count(old), 1, "%s: anchor not unique: %r" % (name, old))
            text = text.replace(old, new)
        path = os.path.join(self.tmp, name + ".py")
        with open(path, "w") as fh:
            fh.write(text)
        return path

    def test_witness_is_green(self):
        rc, red, ran = red_tests(REAL_SCRIPT)
        sys.stderr.write("\nCM0 witness: %d cases, red: %s\n" % (ran, red or "none"))
        self.assertGreater(ran, 20)
        self.assertEqual((rc, red), (0, []))

    def test_every_mutant_turns_the_suite_red(self):
        escaped = []
        for name, edits, must_be_red in MUTANTS:
            rc, red, ran = red_tests(self.mutate(name, edits))
            sys.stderr.write("\nmutant %-30s -> %d red: %s\n" % (name, len(red), ", ".join(red)))
            if rc == 0 or must_be_red not in red:
                escaped.append("%s (expected %s, got %s)" % (name, must_be_red, red))
        self.assertEqual(escaped, [])


if __name__ == "__main__":
    unittest.main()
