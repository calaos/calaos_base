#!/usr/bin/env python3
"""A measure a bounded-echo suite leans on must be pinned to what it returns.

The bounded-echo suites of tests/ do not look for a secret by name: they ask a
measure how much of the input the journal gives back, and bound the answer
against a ceiling.  Every one of those bounds is ONE-SIDED - `EXPECT_LT(m, k)` -
so the answer "nothing" satisfies all of them at once.  A measure that returns
0, because it broke or because it was handed an empty haystack, turns the whole
family green without a single red anywhere.  That is the failure mode this
closes, and it is invisible to check-echo-ceilings.sh, which reads the shape of
the ceiling and never the measure under it.

THE RULE.  In a file that declares a kMax...Echo ceiling, a measure of that
file whose result is bounded only from above against that ceiling must be
PINNED, in a live gtest case of the same file, by two equalities against
LITERALS: one that a working measure answers with something, one that it
answers with nothing.  Both halves are needed - a single positive pin is
satisfied by a measure that never returns empty, and a single empty pin by one
that never returns anything.

An equality against the ceiling itself - `EXPECT_EQ(k, worst.size() + 1)`, the
calibration check-echo-ceilings.sh already demands - is two-sided and needs no
pin: a blinded measure moves it as surely as a widened one.  Only the one-sided
bounds are asked for anything here.

What does not run does not pin either: `#if 0` and DISABLED_ cases are read as
absent, exactly as in check-echo-ceilings.py.  What stays out of reach is
whether the pin exercises the same code path as the bound; the names are the
contract, as they are for the ceilings.

Exit codes follow the automake simple-test protocol: 0 PASS, 1 FAIL.
"""

import os
import re
import sys

CEILING = r'k[A-Za-z0-9_]*Echo'

#`const size_t kMaxEcho = 8;` and its kin - same contract as the sibling probe.
DECL = re.compile(r'(?:^|[{;])\s*(?:static\s+)?(?:const\s+|constexpr\s+)?'
                  r'(?:std::)?(?:size_t|uint\d+_t|unsigned\s+\w+|unsigned|'
                  r'int|long|auto)\s+'
                  r'(' + CEILING + r')\s*=\s*(\d+)\s*;', re.M)

TEST_HEAD = re.compile(r'\bTEST(?:_F|_P)?\s*\(\s*([A-Za-z_]\w*)\s*,\s*'
                       r'([A-Za-z_]\w*)\s*\)\s*\{')

#A measure is any free function of the file whose name says it measures a run.
MEASURE_DEF = re.compile(r'^\s*(?:static\s+)?(?:std::)?(?:string|size_t)\s+'
                         r'(longest[A-Za-z0-9_]*)\s*\(', re.M)

#The one-sided bounds: `EXPECT_LT(<expr>, kMax...Echo)`.
UPPER = re.compile(r'(?:EXPECT|ASSERT)_(?:LT|LE)\s*\(([^;]*?),\s*'
                   r'(' + CEILING + r')\s*\)\s*(?:<<|;)', re.S)

#`const std::string run = longestHexRun(form);`
ASSIGN = re.compile(r'\b(\w+)\s*=\s*(longest[A-Za-z0-9_]*)\s*\(')


#`ASSERT_EQ("bcdef", longestEchoRun(...))` / `ASSERT_EQ(0u, longestEcho(...))`.
#Matched on the masked text, where a literal is blanks: what was written there
#is read back from the raw bytes, so a call dressed as an expected value fails.
PIN = re.compile(r'(?:EXPECT|ASSERT)_EQ\s*\(([^,;()]*),\s*'
                 r'(longest[A-Za-z0-9_]*)\s*\(')


def blank(out, start, end):
    for k in range(max(start, 0), min(end, len(out))):
        if out[k] != '\n':
            out[k] = ' '


def code_mask(text):
    """The same bytes with comments and literal contents blanked out.

    Offsets are preserved, so a match found here indexes the original.
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


def case_blocks(masked):
    """Every gtest case body, as (start, end, runs) in `masked`."""
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
        runs = not (m.group(1).startswith('DISABLED_') or
                    m.group(2).startswith('DISABLED_'))
        blocks.append((m.end(), i, runs))
    return blocks


def bodies(masked, dead):
    """Every outermost brace-balanced block, minus the ones gtest never runs.

    A bound is not always written inside a case: a suite that measures the same
    thing at several sites factors it into a helper, and that helper runs.  One
    region per body rather than one per file, because the local a bound names is
    only the measure it was assigned from IN THE SAME body - two cases reusing
    the name `run` for two measures otherwise blame each other.
    """
    out, depth, start = [], 0, 0
    for i, c in enumerate(masked):
        if c == '{':
            if depth == 0:
                start = i + 1
            depth += 1
        elif c == '}' and depth:
            depth -= 1
            if depth == 0 and not any(a <= start <= b for a, b in dead):
                out.append((start, i))
    return out


def leaned_on(body, known):
    """The measures of this case whose answer only a one-sided bound sees."""
    locals_ = {}
    for m in ASSIGN.finditer(body):
        if m.group(2) in known:
            locals_.setdefault(m.group(1), set()).add(m.group(2))
    found = set()
    for m in UPPER.finditer(body):
        operand = m.group(1)
        direct = {n for n in re.findall(r'(longest[A-Za-z0-9_]*)\s*\(', operand)
                  if n in known}
        if direct:
            found |= direct
            continue
        for var in re.findall(r'\b\w+\b', operand):
            found |= locals_.get(var, set())
    return found


def pinned_in(body, raw, known):
    """The measures this case pins to a literal, split by what it pins them to.

    Returned as (something, nothing): the names pinned to a non-empty answer,
    and those pinned to an empty one.  A pin whose expected side is written in
    the raw text as anything but a literal is not one.
    """
    something, nothing = set(), set()
    for m in PIN.finditer(body):
        name = m.group(2)
        if name not in known:
            continue
        want = raw[m.start(1):m.end(1)].strip()
        if not (re.fullmatch(r'"(?:[^"\\]|\\.)*"', want) or
                re.fullmatch(r'\d+[uUlL]*', want)):
            continue
        empty = want == '""' or re.fullmatch(r'0[uUlL]*', want)
        (nothing if empty else something).add(name)
    return something, nothing


def problems_for(text, rel):
    """The measures of one file that a one-sided bound leans on unpinned."""
    masked = blank_inactive(code_mask(text), text)
    if not DECL.findall(masked):
        return [], 0

    known = set(MEASURE_DEF.findall(masked))
    if not known:
        return [], 0

    blocks = case_blocks(masked)
    dead = [(s, e) for s, e, runs in blocks if not runs]

    leaned = set()
    for start, end in bodies(masked, dead):
        leaned |= leaned_on(masked[start:end], known)

    something, nothing = set(), set()
    for start, end, runs in blocks:
        if not runs:
            continue
        s, n = pinned_in(masked[start:end], text[start:end], known)
        something |= s
        nothing |= n

    problems = []
    for name in sorted(leaned):
        missing = []
        if name not in something:
            missing.append("an answer it gives back")
        if name not in nothing:
            missing.append("the empty answer")
        if missing:
            problems.append(
                "%s: %s() is bounded from above against a bounded-echo ceiling "
                "and nothing pins %s. A measure that answers nothing satisfies "
                "every such bound at once, so blinding it turns the suite green "
                "instead of red. Pin it in a live case: "
                "ASSERT_EQ(\"bcdef\", %s(\"zzbcdefzz\", \"abcdefg\")) and "
                "ASSERT_EQ(\"\", %s(\"zzz\", \"abc\")), on the literals your "
                "measure really answers."
                % (rel, name, " and ".join(missing), name, name))
    return problems, len(leaned)


def scan(root, top):
    """Every .cpp under `root`, and what the sweep saw.

    Returned as (problems, files, measures).  The counts are the point: a walk
    that stops finding files is the way a probe of this family goes silent
    without ever being wrong, so main() refuses an empty sweep.
    """
    problems, files, measures = [], 0, 0
    for dirpath, _dirs, names in os.walk(root):
        for name in sorted(names):
            if not name.endswith('.cpp'):
                continue
            path = os.path.join(dirpath, name)
            files += 1
            with open(path, encoding='utf-8', errors='replace') as fh:
                text = fh.read()
            found, count = problems_for(text, os.path.relpath(path, top))
            problems += found
            measures += count
    return problems, files, measures


#Files written to be refused, and to be accepted, each for one reason.
SELF_TESTS = [
    ("a measure only a one-sided bound sees", 1, "the empty answer", """
const size_t kMaxEcho = 8;
size_t longestEcho(const std::string &a, const std::string &b) { return 0; }
TEST(S, C) { EXPECT_LT(longestEcho(log, s), kMaxEcho); }
"""),
    ("a measure pinned both ways", 0, None, """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a measure pinned only on what it finds", 1, "the empty answer", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a measure pinned only on what it does not find", 1, "an answer it gives back", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pin against another measure instead of a literal", 1, "an answer it gives back", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  ASSERT_EQ(reference(log, s), longestEchoRun("zzbcdefzz", "abcdefg"));
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pin the preprocessor drops", 1, "an answer it gives back", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
#if 0
TEST(S, P) { ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg")); }
#endif
TEST(S, C) {
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pin in a case gtest never runs", 1, "an answer it gives back", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, DISABLED_P) { ASSERT_EQ("bcdef", longestEchoRun("zzbcdefzz", "abcdefg")); }
TEST(S, C) {
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), kMaxEcho); }
"""),
    ("a pin a failure message only talks about", 1, "an answer it gives back", """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  ASSERT_EQ("", longestEchoRun("zzz", "abc"));
  const std::string r = longestEchoRun(log, s);
  EXPECT_LT(r.size(), kMaxEcho) << "ASSERT_EQ(\\"bcdef\\", longestEchoRun(a, b))"; }
"""),
    ("a bound in a case gtest never runs", 0, None, """
const size_t kMaxEcho = 8;
size_t longestEcho(const std::string &a, const std::string &b) { return 0; }
TEST(S, DISABLED_C) { EXPECT_LT(longestEcho(log, s), kMaxEcho); }
"""),
    ("a measure only a two-sided equality sees", 0, None, """
const size_t kMaxEcho = 8;
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) {
  std::string worst = longestEchoRun(log, s);
  EXPECT_EQ(kMaxEcho, worst.size() + 1) << "m"; }
"""),
    ("a file with no ceiling at all", 0, None, """
std::string longestEchoRun(const std::string &a, const std::string &b) { return a; }
TEST(S, C) { const std::string r = longestEchoRun(log, s); EXPECT_LT(r.size(), 8); }
"""),
]


def self_test():
    """The check, run against files written to be refused, and on an empty tree.

    Nothing else in tests/ exercises a static probe.  The last two cases are
    about the INPUT rather than the rule: a probe whose corpus has gone empty
    accuses nobody and reads exactly like a clean tree.
    """
    bad = []
    for label, want, said, text in SELF_TESTS:
        problems, _seen = problems_for(text, "<self-test>")
        got = " ".join(problems)
        if len(problems) != want:
            bad.append("%s: %d complaint(s), expected %d (%s)"
                       % (label, len(problems), want, got or "none"))
        elif said and said not in got:
            bad.append("%s: complaint does not say \"%s\" (%s)"
                       % (label, said, got or "none"))

    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        _p, files, measures = scan(tmp, tmp)
        if (files, measures) != (0, 0):
            bad.append("an empty tree: %d file(s), %d measure(s), expected none"
                       % (files, measures))
        with open(os.path.join(tmp, 'x.cpp'), 'w', encoding='utf-8') as fh:
            fh.write(SELF_TESTS[0][3])
        problems, files, measures = scan(tmp, tmp)
        if (len(problems), files, measures) != (1, 1, 1):
            bad.append("a tree of one refused file: %d complaint(s), %d file(s), "
                       "%d measure(s), expected 1, 1, 1"
                       % (len(problems), files, measures))

    for b in bad:
        print("FAIL: self-test: " + b)
    if bad:
        return False
    print("PASS: self-test, %d files written to be refused and %d to be "
          "accepted, plus the sweep itself on an empty tree and on one file"
          % (sum(1 for t in SELF_TESTS if t[1]),
             sum(1 for t in SELF_TESTS if not t[1])))
    return True


def main():
    if not self_test():
        return 1
    if '--self-test' in sys.argv[1:]:
        return 0

    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    problems, files, measures = scan(os.path.join(top, 'tests'), top)

    if not files:
        print("FAIL: the sweep read no file at all under tests/ - a probe with "
              "an empty corpus accuses nobody and looks exactly like a clean "
              "tree.")
        return 1

    if problems:
        for p in problems:
            print("FAIL: " + p)
        print("%d measure(s) under a one-sided echo bound checked over %d file(s), "
              "%d unpinned" % (measures, files, len(problems)))
        return 1

    print("PASS: %d measure(s) under a one-sided echo bound over %d file(s), each "
          "pinned to what it answers and to what it answers on nothing"
          % (measures, files))
    return 0


if __name__ == '__main__':
    sys.exit(main())
