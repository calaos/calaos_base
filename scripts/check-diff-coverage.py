#!/usr/bin/env python3
"""Diff-coverage gate: every executable line added under src/ must be run.

    check-diff-coverage.py --info coverage.info --base origin/master
    check-diff-coverage.py --info coverage.info --diff pr.diff --root .

Exit codes: 0 gate passed, 1 uncovered or unjustified lines, 2 unusable input.
Exclusions are the lcov markers LCOV_EXCL_LINE and LCOV_EXCL_START/STOP, each
followed by a reason; the number of excluded lines the diff adds is always
printed so a reviewer sees what was opted out.
"""
import argparse
import os
import re
import subprocess
import sys

UNIT_EXT = (".c", ".cc", ".cpp", ".cxx")
CODE_EXT = UNIT_EXT + (".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp")

# Same trees the CI job strips from the tracefile; a change there has no data
# by construction and must not be reported as "never compiled".
VENDORED = (
    "src/lib/libquickmail/", "src/lib/uvw/", "src/lib/llhttp/", "src/lib/exprtk/",
    "src/lib/sole/", "src/lib/sqlite_modern_cpp/", "src/lib/uri_parser/",
    "src/lib/cpptui/", "src/lib/pugixml/", "src/lib/json.hpp",
)

MARKER = re.compile(r"LCOV_EXCL_(LINE|START|STOP)\b(.*)")
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


class InputError(Exception):
    pass


def parse_tracefile(text, root):
    """path relative to root -> {line: summed hit count}."""
    files = {}
    cur = None
    for raw in text.splitlines():
        if raw.startswith("SF:"):
            sf = os.path.normpath(raw[3:].strip())
            if os.path.isabs(sf):
                sf = os.path.relpath(sf, root)
            cur = files.setdefault(sf, {})
        elif raw.startswith("DA:") and cur is not None:
            fields = raw[3:].split(",")
            if len(fields) < 2:
                raise InputError("malformed DA record: " + raw)
            line, hits = int(fields[0]), int(fields[1])
            cur[line] = cur.get(line, 0) + hits
        elif raw == "end_of_record":
            cur = None
    return files


def _unquote(path):
    if path.startswith('"') and path.endswith('"'):
        path = path[1:-1].encode().decode("unicode_escape").encode("latin-1").decode("utf-8")
    return path


def parse_diff(text):
    """new path -> sorted added line numbers, from a -U0 unified diff."""
    added = {}
    cur = None
    for raw in text.splitlines():
        if raw.startswith("+++ "):
            target = _unquote(raw[4:].split("\t")[0])
            if target == "/dev/null":
                cur = None
            else:
                cur = target[2:] if target.startswith("b/") else target
                added.setdefault(cur, set())
        elif raw.startswith("--- "):
            continue
        elif cur is not None:
            m = HUNK.match(raw)
            if m:
                start = int(m.group(1))
                count = 1 if m.group(2) is None else int(m.group(2))
                added[cur].update(range(start, start + count))
    return {p: sorted(v) for p, v in added.items()}


def scan_exclusions(lines, added):
    """Lines lcov will drop, plus markers added without a reason."""
    excluded = set()
    problems = []
    open_at = None
    for no, text in enumerate(lines, 1):
        m = MARKER.search(text)
        if not m:
            if open_at is not None:
                excluded.add(no)
            continue
        kind = m.group(1)
        reason = m.group(2).replace("*/", "").strip(" \t:-–—")
        if kind == "LINE":
            excluded.add(no)
        elif kind == "START":
            open_at = open_at or no
            excluded.add(no)
        else:
            excluded.add(no)
            open_at = None
        if no in added and kind != "STOP" and not reason:
            problems.append((no, "LCOV_EXCL_%s without a reason" % kind))
    if open_at is not None and open_at in added:
        problems.append((open_at, "LCOV_EXCL_START never closed"))
    return excluded, problems


def is_trivia(text):
    s = text.strip()
    return not s or s.startswith(("//", "/*", "*"))


def ranges(numbers):
    out = []
    for n in sorted(numbers):
        if out and n == out[-1][1] + 1:
            out[-1][1] = n
        else:
            out.append([n, n])
    return out


def evaluate(coverage, diff, root):
    res = {"added": 0, "non_exec": 0, "covered": 0, "uncovered": [], "excluded": [],
           "nodata": [], "problems": [], "skipped": 0}
    for path, added in sorted(diff.items()):
        if (not path.startswith("src/") or not path.endswith(CODE_EXT)
                or path.startswith(VENDORED)):
            res["skipped"] += len(added)
            continue
        if not added:
            continue
        try:
            with open(os.path.join(root, path), encoding="utf-8", errors="replace") as fh:
                src = fh.read().splitlines()
        except OSError as exc:
            raise InputError("cannot read %s: %s" % (path, exc))
        excluded, problems = scan_exclusions(src, set(added))
        res["problems"] += [(path, n, why) for n, why in problems]
        res["added"] += len(added)
        data = coverage.get(path)
        for n in added:
            if n in excluded:
                res["excluded"].append((path, n, src[n - 1].strip()))
            elif data is None:
                # A header holding only declarations has no record at all, so
                # silence is only suspicious for a translation unit.
                if path.endswith(UNIT_EXT) and n <= len(src) and not is_trivia(src[n - 1]):
                    res["nodata"].append((path, n, src[n - 1].strip()))
                else:
                    res["non_exec"] += 1
            elif n not in data:
                res["non_exec"] += 1
            elif data[n] > 0:
                res["covered"] += 1
            else:
                res["uncovered"].append((path, n, src[n - 1].strip()))
    return res


def git_diff(base, root):
    cmd = ["git", "-C", root, "diff", "--no-color", "--no-ext-diff", "-U0", "-M",
           base + "...HEAD", "--", "src"]
    done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if done.returncode != 0:
        raise InputError("git diff failed: " + done.stderr.decode(errors="replace").strip())
    return done.stdout.decode("utf-8", errors="replace")


def report(res, out):
    w = out.write
    if res["uncovered"]:
        w("Added lines never executed by make check:\n")
        for path, n, text in res["uncovered"]:
            w("  %s:%d: %s\n" % (path, n, text))
    if res["nodata"]:
        w("Added lines in files with no coverage data (not compiled by this build?):\n")
        for path, n, text in res["nodata"]:
            w("  %s:%d: %s\n" % (path, n, text))
    for path, n, why in res["problems"]:
        w("Unjustified exclusion: %s:%d: %s\n" % (path, n, why))
    total_excl = len(res["excluded"])
    w("Excluded lines added by this diff: %d\n" % total_excl)
    by_file = {}
    for path, n, _ in res["excluded"]:
        by_file.setdefault(path, []).append(n)
    for path, nums in by_file.items():
        spans = ", ".join(str(a) if a == b else "%d-%d" % (a, b) for a, b in ranges(nums))
        w("  %s: %s\n" % (path, spans))
    w("diff-coverage: %d added lines under src/ (C/C++), %d covered, %d not executable, "
      "%d excluded, %d uncovered, %d without data; %d added lines outside the gate\n"
      % (res["added"], res["covered"], res["non_exec"], total_excl,
         len(res["uncovered"]), len(res["nodata"]), res["skipped"]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--info", required=True, help="lcov tracefile")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--base", help="git ref; diffs BASE...HEAD")
    src.add_argument("--diff", help="unified diff file (-U0), instead of git")
    ap.add_argument("--root", default=".", help="source tree the diff applies to")
    ap.add_argument("--report-only", action="store_true", help="print, always exit 0")
    args = ap.parse_args(argv)
    root = os.path.abspath(args.root)

    try:
        with open(args.info, encoding="utf-8", errors="replace") as fh:
            coverage = parse_tracefile(fh.read(), root)
        if not coverage:
            raise InputError("%s holds no SF record: an empty tracefile would pass anything"
                             % args.info)
        if args.diff:
            with open(args.diff, encoding="utf-8", errors="replace") as fh:
                diff = parse_diff(fh.read())
        else:
            diff = parse_diff(git_diff(args.base, root))
        res = evaluate(coverage, diff, root)
    except (InputError, OSError) as exc:
        sys.stderr.write("diff-coverage: %s\n" % exc)
        return 2

    report(res, sys.stdout)
    if os.environ.get("GITHUB_ACTIONS"):
        for path, n, _ in (res["uncovered"] + res["nodata"])[:10]:
            print("::error file=%s,line=%d::new line not executed by make check" % (path, n))
    failed = res["uncovered"] or res["nodata"] or res["problems"]
    if failed and args.report_only:
        print("diff-coverage: --report-only, not failing")
        return 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
