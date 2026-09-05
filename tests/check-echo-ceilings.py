#!/usr/bin/env python3
"""Every bounded-echo ceiling of tests/ must be held by the suite itself.

A handful of suites do not look for a secret by name: they bound the longest
run of the input the journal gives back, against a ceiling.  The number is not
a setting, it is the incidental overlap between the fixture and the lines the
code is entitled to print - and that overlap moves with the fixture.  Written
by hand and revisited by nobody, the ceiling drifts: too wide, and a leak of
everything under it is invisible; too narrow, and the suite reddens on noise.

The rule, per declared ceiling: it is PINNED - some EXPECT_EQ/ASSERT_EQ in the
same file compares it to a re-measured `<something>.size() + 1` - or it is HELD
above a pinned ceiling of the same file by an EXPECT_GT.  A document nobody
chose - a token drawn by the shipped generator, in the same alphabet as the
identifiers the journal draws for itself - has no overlap to re-measure, only a
probability; pinning it would put a lottery inside an equality.  Such a ceiling
is legitimate, but only as a stated margin above one that IS pinned.  Anything
else is a ceiling nothing re-derives, which is the defect.

This check reads the SHAPE and never the measure: it cannot tell a calibration
that replays every probe from one that replays a single one.

Exit codes follow the automake simple-test protocol: 0 PASS, 1 FAIL.
"""

import os
import re
import sys

#`const size_t kMaxEcho = 8;` and its kin. The name is the contract; the type
#and the constness are not, so neither narrows what gets checked.
DECL = re.compile(r'(?:^|[{;])\s*(?:static\s+)?(?:const\s+|constexpr\s+)?'
                  r'(?:std::)?(?:size_t|uint\d+_t|unsigned\s+\w+|unsigned|'
                  r'int|long|auto)\s+'
                  r'(k[A-Za-z0-9_]*Echo)\s*=\s*(\d+)\s*;', re.M)


def pinned_names(text):
    """Ceilings compared to a re-measured length, anywhere in the file."""
    out = set()
    for m in re.finditer(r'(?:EXPECT_EQ|ASSERT_EQ)\s*\(\s*(k[A-Za-z0-9_]*Echo)\s*,'
                         r'([^;]*?)\)\s*(?:<<|;)', text, re.S):
        if re.search(r'\.size\s*\(\s*\)\s*\+\s*1', m.group(2)):
            out.add(m.group(1))
    return out


def held_above(text):
    """Ceilings declared strictly wider than another ceiling of the file."""
    out = {}
    for m in re.finditer(r'(?:EXPECT_GT|ASSERT_GT)\s*\(\s*(k[A-Za-z0-9_]*Echo)\s*,\s*'
                         r'(k[A-Za-z0-9_]*Echo)\s*\)', text):
        out.setdefault(m.group(1), set()).add(m.group(2))
    return out


def main():
    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    root = os.path.join(top, 'tests')

    problems = []
    seen = 0
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith('.cpp'):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding='utf-8', errors='replace') as fh:
                text = fh.read()

            decls = DECL.findall(text)
            if not decls:
                continue

            rel = os.path.relpath(path, top)
            pinned = pinned_names(text)
            above = held_above(text)
            for const, _value in decls:
                seen += 1
                if const in pinned:
                    continue
                if any(other in pinned for other in above.get(const, ())):
                    continue
                problems.append(
                    "%s: %s is a bounded-echo ceiling nothing re-derives. "
                    "Either pin it - EXPECT_EQ(%s, worst.size() + 1) on the "
                    "overlap the suite re-measures at every run - or hold it "
                    "above a pinned ceiling of the same file with EXPECT_GT."
                    % (rel, const, const))

    if problems:
        for p in problems:
            print("FAIL: " + p)
        print("%d bounded-echo ceiling(s) checked, %d unheld"
              % (seen, len(problems)))
        return 1

    print("PASS: %d bounded-echo ceiling(s), each pinned or held above a "
          "pinned one" % seen)
    return 0


if __name__ == '__main__':
    sys.exit(main())
