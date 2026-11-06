#!/usr/bin/env python3
"""Every bounded-echo ceiling of tests/ must be held by the suite itself.

A handful of suites do not look for a secret by name: they bound the longest
run of the input the journal gives back, against a ceiling.  The number is not
a setting, it is the incidental overlap between the fixture and the lines the
code is entitled to print - and that overlap moves with the fixture.  Written
by hand and revisited by nobody, the ceiling drifts: too wide, and a leak of
everything under it is invisible; too narrow, and the suite reddens on noise.

Two rules, per declared ceiling.

RE-DERIVED.  It is PINNED - some EXPECT_EQ/ASSERT_EQ in the same file compares
it to a re-measured `<something>.size() + 1` - or it is HELD above a pinned
ceiling of the same file by an EXPECT_GT.  A document nobody chose - a token
drawn by the shipped generator, in the same alphabet as the identifiers the
journal draws for itself - has no overlap to re-measure, only a probability;
pinning it would put a lottery inside an equality.  Such a ceiling is
legitimate, but only as a stated margin above one that IS pinned.

STABLE.  A pinned equality is only reproducible if the bytes nobody chose
cannot lengthen a run.  The journal prints identifiers of its own in
hexadecimal, so a document carrying a hexadecimal run as long as the overlap
turns the equality into a lottery, and its red into the intermittent kind
nobody can reproduce.  The file must therefore bound that run against the same
ceiling, in a case that MEASURES it.  The bound belongs on every form the
measure hunts, not on the literal: percent encoding and base64 draw in that
alphabet on their own.

What does not run does not hold either: a pinning inside `#if 0`, inside a
DISABLED_ case, or compared against a written value instead of a measure, is
read here as absent.  What remains out of reach is the measure itself - this
cannot tell a calibration that replays every probe from one that replays a
single one, and a ceiling named otherwise than kMax...Echo is invisible to it.

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

TEST_HEAD = re.compile(r'\bTEST(?:_F|_P)?\s*\(\s*([A-Za-z_]\w*)\s*,\s*'
                       r'([A-Za-z_]\w*)\s*\)\s*\{')

PIN = re.compile(r'(?:EXPECT_EQ|ASSERT_EQ)\s*\(\s*(k[A-Za-z0-9_]*Echo)\s*,'
                 r'([^;]*?)\)\s*(?:<<|;)', re.S)
GT = re.compile(r'(?:EXPECT_GT|ASSERT_GT)\s*\(\s*(k[A-Za-z0-9_]*Echo)\s*,\s*'
                r'(k[A-Za-z0-9_]*Echo)\s*\)')
LT = re.compile(r'(?:EXPECT_LT|ASSERT_LT)\s*\(([^;]*?),\s*'
                r'(k[A-Za-z0-9_]*Echo)\s*\)\s*(?:<<|;)', re.S)
HEX_MEASURE = re.compile(r'\b\w*[Hh]ex\w*Run\s*\(')
REMEASURED = re.compile(r'\.size\s*\(\s*\)\s*\+\s*1')


def blank(out, start, end):
    for k in range(max(start, 0), min(end, len(out))):
        if out[k] != '\n':
            out[k] = ' '


def code_mask(text):
    """The same bytes with comments and literal contents blanked out.

    Offsets are preserved, so a match found here indexes the original: brace
    matching and argument capture stop tripping over a `}` or a `,` written
    inside a failure message.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
        elif c in '"\'':
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == c or text[j] == '\n':
                    j += 1
                    break
                j += 1
        else:
            i += 1
            continue
        blank(out, i, j)
        i = max(j, i + 1)
    return ''.join(out)


def blank_inactive(masked, text):
    """Blank what the preprocessor drops: `#if 0` up to its `#else`/`#endif`."""
    out = list(masked)
    depth, start, off = 0, 0, 0
    for line in text.split('\n'):
        s, eol = line.strip(), off + len(line)
        if depth:
            if re.match(r'#\s*if', s):
                depth += 1
            elif re.match(r'#\s*endif\b', s):
                depth -= 1
                if depth == 0:
                    blank(out, start, eol)
            elif depth == 1 and re.match(r'#\s*(else|elif)\b', s):
                blank(out, start, off)
                depth = 0
        elif re.match(r'#\s*if\s+0\b', s):
            depth, start = 1, off
        off = eol + 1
    if depth:
        blank(out, start, len(out))
    return ''.join(out)


def live_blocks(masked):
    """Every gtest case body gtest really runs, as (start, end) in `masked`."""
    blocks = []
    for m in TEST_HEAD.finditer(masked):
        depth, i, n = 0, m.end() - 1, len(masked)
        while i < n:
            if masked[i] == '{':
                depth += 1
            elif masked[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if not (m.group(1).startswith('DISABLED_') or
                m.group(2).startswith('DISABLED_')):
            blocks.append((m.end(), i))
    return blocks


def measures(raw_arg):
    """A length read off something, as opposed to a value written by hand."""
    return '.size' in raw_arg and '"' not in raw_arg and "'" not in raw_arg


def problems_for(text, rel):
    """The ceilings of one file that nothing re-derives or nothing stabilises."""
    masked = blank_inactive(code_mask(text), text)

    decls = [c for c, _v in DECL.findall(masked)]
    if not decls:
        return [], 0

    pinned, above, hexed = set(), {}, set()
    for start, end in live_blocks(masked):
        body, raw = masked[start:end], text[start:end]

        for m in PIN.finditer(body):
            if REMEASURED.search(m.group(2)) and measures(raw[m.start(2):m.end(2)]):
                pinned.add(m.group(1))

        for m in GT.finditer(body):
            above.setdefault(m.group(1), set()).add(m.group(2))

        #The hexadecimal bound is recognised by the company it keeps: a case
        #that calls the measure and bounds a length against the ceiling.
        if HEX_MEASURE.search(body):
            for m in LT.finditer(body):
                if measures(raw[m.start(1):m.end(1)]):
                    hexed.add(m.group(2))

    problems = []
    for const in decls:
        if const not in pinned:
            if any(other in pinned for other in above.get(const, ())):
                continue
            problems.append(
                "%s: %s is a bounded-echo ceiling nothing re-derives. Either "
                "pin it - EXPECT_EQ(%s, worst.size() + 1) on the overlap the "
                "suite re-measures at every run - or hold it above a pinned "
                "ceiling of the same file with EXPECT_GT."
                % (rel, const, const))
        elif const not in hexed:
            problems.append(
                "%s: %s is pinned, but nothing bounds the hexadecimal scope of "
                "the fixture under it. The journal prints identifiers of its "
                "own in that alphabet, so a document carrying a run that long "
                "makes the equality a lottery whose red is intermittent. Add a "
                "case measuring the longest hexadecimal run of EVERY form the "
                "suite hunts - clear, percent encoded, base64 - and bound it: "
                "EXPECT_LT(run.size(), %s)." % (rel, const, const))
    return problems, len(decls)


#Eight files written to be refused, each for one reason, plus the two shapes
#that must be accepted. `held` is how many of the file's ceilings come out held.
SELF_TESTS = [
    ("a bare ceiling", 0, "nothing re-derives", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_LT(x, kMaxEcho); }
"""),
    ("a pinned and hexadecimal-bounded ceiling", 1, None, """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pinned ceiling with no hexadecimal bound", 0, "hexadecimal scope", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
"""),
    ("a hexadecimal bound on a written value", 0, "hexadecimal scope", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
TEST(S, H) { longestHexRun(d); EXPECT_LT(std::string("ab").size(), kMaxEcho); }
"""),
    ("a pinning against a literal", 0, "nothing re-derives", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_EQ(kMaxEcho, std::string("abc").size() + 1) << "m"; }
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pinning the preprocessor drops", 0, "nothing re-derives", """
const size_t kMaxEcho = 8;
#if 0
TEST(S, C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
#endif
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pinning in a case gtest never runs", 0, "nothing re-derives", """
const size_t kMaxEcho = 8;
TEST(S, DISABLED_C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a hexadecimal bound in a case gtest never runs", 0, "hexadecimal scope", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
TEST(S, DISABLED_H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a drawn ceiling held above a pinned one", 2, None, """
const size_t kMaxEcho = 5;
const size_t kMaxDrawnEcho = 8;
TEST(S, C) { EXPECT_GT(kMaxDrawnEcho, kMaxEcho); EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pinning a failure message only talks about", 0, "nothing re-derives", """
const size_t kMaxEcho = 8;
TEST(S, C) { EXPECT_TRUE(b) << "EXPECT_EQ(kMaxEcho, worst.size() + 1)"; }
TEST(S, H) { const std::string r = longestHexRun(d); EXPECT_LT(r.size(), kMaxEcho); }
"""),
]


def self_test():
    """The check, run against files written to be refused.

    Nothing else in tests/ exercises a static probe: without this, the only way
    to know one still speaks is to mutate a suite by hand.
    """
    bad = []
    for label, want_held, want_said, text in SELF_TESTS:
        problems, seen = problems_for(text, "<self-test>")
        held = seen - len(problems)
        said = " ".join(problems)
        if held != want_held:
            bad.append("%s: %d ceiling(s) held, expected %d (%s)"
                       % (label, held, want_held, said or "no complaint"))
        elif want_said and want_said not in said:
            bad.append("%s: complaint does not say \"%s\" (%s)"
                       % (label, want_said, said or "no complaint"))
    for b in bad:
        print("FAIL: self-test: " + b)
    if bad:
        return False
    print("PASS: self-test, %d files written to be refused and %d to be "
          "accepted, each read as meant"
          % (sum(1 for t in SELF_TESTS if t[2]),
             sum(1 for t in SELF_TESTS if not t[2])))
    return True


def main():
    if not self_test():
        return 1
    if '--self-test' in sys.argv[1:]:
        return 0

    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    root = os.path.join(top, 'tests')

    problems, seen = [], 0
    for dirpath, _dirs, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith('.cpp'):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding='utf-8', errors='replace') as fh:
                text = fh.read()
            found, count = problems_for(text, os.path.relpath(path, top))
            problems += found
            seen += count

    if problems:
        for p in problems:
            print("FAIL: " + p)
        print("%d bounded-echo ceiling(s) checked, %d unheld"
              % (seen, len(problems)))
        return 1

    print("PASS: %d bounded-echo ceiling(s), each re-derived by its suite and "
          "each fixture held out of the journal's alphabet" % seen)
    return 0


if __name__ == '__main__':
    sys.exit(main())
