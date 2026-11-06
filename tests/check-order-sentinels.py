#!/usr/bin/env python3
"""No ordering assertion of tests/ may accept a "not found" answer.

The characterization suites do not compare documents, they compare POSITIONS:
`EXPECT_LT(rankOf(env, "data"), rankOf(env, "msg"))` says data comes first on
the wire.  Every such helper answers something for a key that is not there, and
that something is an ordinary integer the comparison happily accepts:

  * a rank helper answers -1, which is BELOW every real rank, so the key
    vanishing from the payload makes the `<` TRUE;
  * a position helper - anything built on std::string::find() - answers npos,
    which is ABOVE every real position, so the key vanishing from the RIGHT
    hand side makes the `<` TRUE.

Either way the case turns green exactly when the thing it guards disappears.
It is a factory for vacuous cases, and the greens it produces are the ones
nobody looks at.

THE RULE.  In the accepting slot - the low side for a negative sentinel, the
high side for npos - the value compared must be ruled out of its sentinel by an
assertion of the same case, naming the same expression: ASSERT_GE(<it>, 0), or
ASSERT_NE(std::string::npos, <it>).  Whether the guard is written before or
after does not matter: what matters is that a missing key cannot leave the case
green.  A value bound to a local is guarded through that local, which is the
shape to prefer - the guard and the comparison then read the same bytes.

WHAT THIS CANNOT SEE.  It reads one file at a time, so a helper defined in a
header is not classified and its call sites are invisible.  It knows two
sentinels; a helper answering 0 or an empty string for absent is the same
class and passes unseen.  Iterator orderings - two std::find() into the same
container, where end() plays the part of npos - are out of reach: their
polarity depends on the container, not on the text.  And it says nothing about
a measure that is merely BLIND: a length that is 0 because nothing was captured
satisfies every `<` there is, and no ordering of positions is involved.

Exit codes follow the automake simple-test protocol: 0 PASS, 1 FAIL.
"""

import os
import re
import sys

ORDER = re.compile(r'\b(?:EXPECT|ASSERT)_(LT|LE|GT|GE)\s*\(')

#A file-local function definition. The body decides the polarity; the return
#type does not, because half of these helpers hand an int back for a size_t.
DEF = re.compile(r'(?m)^[ \t]*(?:(?:static|inline|const|constexpr|virtual|'
                 r'explicit)\s+)*(?:[\w:]+(?:\s*<[^;{}]*?>)?)'
                 r'(?:\s*[*&]+\s*|\s+)([A-Za-z_]\w*)\s*\(([^;{}]*)\)'
                 r'\s*(?:const\s*)?(?:noexcept\s*)?\{')

NOT_A_HELPER = frozenset((
    'TEST', 'TEST_F', 'TEST_P', 'if', 'for', 'while', 'switch', 'catch',
    'return', 'else', 'do', 'sizeof',
))

TEST_HEAD = re.compile(r'\bTEST(?:_F|_P)?\s*\(\s*([A-Za-z_]\w*)\s*,\s*'
                       r'([A-Za-z_]\w*)\s*\)\s*\{')

RETURNS_NEGATIVE = re.compile(r'\breturn\b[^;]*?(?<![\w.])-\s*\d+')
RETURNS_POSITION = re.compile(r'\breturn\b[^;]*?(?:\.r?find\s*\(|npos)')

CALL = re.compile(r'^([A-Za-z_]\w*)\((.*)\)$', re.S)
LOOKUP = re.compile(r'^([A-Za-z_][\w:.\[\]]*)\.(?:r?find)\((.*)\)$', re.S)
IDENT = re.compile(r'^[A-Za-z_]\w*$')
BIND = r'(?:^|[;{}()])[^;{}]*?\b%s\s*=\s*([^;]+);'

NPOS = frozenset(('std::string::npos', 'string::npos', 'npos'))

#The accepting slot, per assertion and per sentinel: the one where a sentinel
#SATISFIES the comparison instead of breaking it.
ACCEPTS = {'LT': {'max': 1, 'min': 0}, 'LE': {'max': 1, 'min': 0},
           'GT': {'max': 0, 'min': 1}, 'GE': {'max': 0, 'min': 1}}

SAYS = {'max': "ASSERT_NE(std::string::npos, %s)",
        'min': "ASSERT_GE(%s, 0)"}

WHY = {'max': "answers std::string::npos, which is ABOVE every real position",
       'min': "answers a negative rank, which is BELOW every real rank"}


def blank(out, start, end):
    for k in range(max(start, 0), min(end, len(out))):
        if out[k] != '\n':
            out[k] = ' '


def code_mask(text):
    """The same bytes with comments and literal CONTENTS blanked out.

    Offsets are preserved, so a match found here indexes the original: brace
    matching and argument splitting stop tripping over a `,` or a `)` written
    inside a failure message. The second answer marks what was a literal, which
    is what tells a blanked comment from bytes that must survive normalising.
    """
    out, lit = list(text), [False] * len(text)
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
            for k in range(i, min(j, n)):
                lit[k] = True
        else:
            i += 1
            continue
        blank(out, i, j)
        i = max(j, i + 1)
    return ''.join(out), lit


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


def close_brace(masked, open_at):
    """Offset of the `}` matching the `{` at open_at, or len(masked)."""
    depth, i, n = 0, open_at, len(masked)
    while i < n:
        if masked[i] == '{':
            depth += 1
        elif masked[i] == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n


def live_blocks(masked):
    """Every gtest case body gtest really runs, as (start, end) in `masked`."""
    blocks = []
    for m in TEST_HEAD.finditer(masked):
        end = close_brace(masked, m.end() - 1)
        if not (m.group(1).startswith('DISABLED_') or
                m.group(2).startswith('DISABLED_')):
            blocks.append((m.end(), end))
    return blocks


def squash(raw, masked, lit):
    """`raw`, comments dropped and formatting whitespace normalised away.

    Comparing helper calls by text is what lets a guard be matched to the value
    it guards, so two spellings of one call must come out identical: whitespace
    touching punctuation goes, the rest collapses to one space, and the bytes
    INSIDE a literal survive untouched - "msg" and "msg_id" are what tell two
    guards apart.
    """
    kept = [(c if l else m) for c, m, l in zip(raw, masked, lit)]
    word = re.compile(r'\w')
    out, n = [], len(kept)
    i = 0
    while i < n:
        c = kept[i]
        if not c.isspace():
            out.append(c)
            i += 1
            continue
        j = i
        while j < n and kept[j].isspace():
            j += 1
        before = out[-1] if out else ''
        after = kept[j] if j < n else ''
        if before and after and word.match(before) and word.match(after):
            out.append(' ')
        i = j
    return ''.join(out)


def split_args(s):
    """Top-level comma split of an already-squashed argument list."""
    args, depth, start = [], 0, 0
    for i, c in enumerate(s):
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ',' and depth == 0:
            args.append(s[start:i])
            start = i + 1
    args.append(s[start:])
    return args


def balanced(s):
    """Bracket depth of `s`, blind to what a string or char literal holds.

    A needle is allowed to carry an unmatched bracket - find("ubyte[") - and
    counting it would leave the whole slot unclassified, hence unseen.
    """
    depth, i, n = 0, 0, len(s)
    while i < n:
        c = s[i]
        if c in '"\'':
            i += 1
            while i < n and s[i] != c:
                i += 2 if s[i] == '\\' else 1
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth < 0:
                return False
        i += 1
    return depth == 0


def helpers_of(masked):
    """File-local helpers, by name, mapped to the sentinels they can answer."""
    kinds = {}
    for m in DEF.finditer(masked):
        name = m.group(1)
        if name in NOT_A_HELPER:
            continue
        body = masked[m.end() - 1:close_brace(masked, m.end() - 1)]
        seen = set()
        if RETURNS_NEGATIVE.search(body):
            seen.add('min')
        if RETURNS_POSITION.search(body):
            seen.add('max')
        if seen:
            kinds.setdefault(name, set()).update(seen)
    return kinds


def polarities(expr, helpers, body, depth=0):
    """Which sentinels `expr` can evaluate to, and the text a guard must name.

    A bare identifier is resolved once through its initialiser in the case, so
    the shape the tree already prefers - bind, guard the local, compare the
    local - is read for what it is.
    """
    if not expr or not balanced(expr):
        return set(), expr
    m = LOOKUP.match(expr)
    if m and balanced(m.group(2)):
        return {'max'}, expr
    m = CALL.match(expr)
    if m and balanced(m.group(2)):
        return set(helpers.get(m.group(1), ())), expr
    if IDENT.match(expr) and depth == 0:
        binds = re.findall(BIND % re.escape(expr), body)
        if binds:
            kinds, _ = polarities(binds[-1], helpers, body, depth + 1)
            return kinds, expr
    return set(), expr


def unrolled(body):
    """`body` plus one copy of each range-for over a braced list of literals.

    A guard written once for a loop variable covers every needle of the list,
    and refusing that shape would push suites towards repeating themselves -
    which is how a probe gets disarmed.
    """
    out = [body]
    for m in re.finditer(r'\bfor\(([^;()]*?):\{([^{}]*)\}\)', body):
        var = re.findall(r'([A-Za-z_]\w*)$', m.group(1))
        if not var:
            continue
        rest = body[m.end():]
        if rest.startswith('{'):
            inner = rest[1:close_brace(rest, 0)]
        else:
            inner = rest.split(';', 1)[0]
        for lit in split_args(m.group(2)):
            out.append(re.sub(r'\b%s\b' % re.escape(var[0]), lit, inner))
    return '\n'.join(out)


def guarded(key, kind, body):
    """Does the case rule `key` out of the `kind` sentinel anywhere at all?"""
    k = re.escape(key)
    if kind == 'max':
        pats = [r'_NE\((?:%s),%s\)' % ('|'.join(re.escape(n) for n in NPOS), k),
                r'_NE\(%s,(?:%s)\)' % (k, '|'.join(re.escape(n) for n in NPOS))]
    else:
        pats = [r'_GE\(%s,0\)' % k, r'_LE\(0,%s\)' % k, r'_GT\(%s,-1\)' % k,
                r'_NE\(%s,-1\)' % k, r'_NE\(-1,%s\)' % k]
    return any(re.search(p, body) for p in pats)


def problems_for(text, rel):
    """The ordering assertions of one file that accept a "not found" answer."""
    masked, lit = code_mask(text)
    masked = blank_inactive(masked, text)
    helpers = helpers_of(masked)

    problems, seen = [], 0
    for start, end in live_blocks(masked):
        body = unrolled(squash(text[start:end], masked[start:end],
                                lit[start:end]))

        for m in ORDER.finditer(masked, start, end):
            depth, i, n = 1, m.end(), end
            while i < n and depth:
                if masked[i] in '([{':
                    depth += 1
                elif masked[i] in ')]}':
                    depth -= 1
                i += 1
            args = split_args(squash(text[m.end():i - 1],
                                     masked[m.end():i - 1],
                                     lit[m.end():i - 1]))
            if len(args) < 2:
                continue

            for kind, slot in ACCEPTS[m.group(1)].items():
                kinds, key = polarities(args[slot], helpers, body)
                if kind not in kinds:
                    continue
                seen += 1
                if not guarded(key, kind, body):
                    problems.append(
                        "%s: %s sits on the accepting side of a %s and %s. "
                        "The case stays GREEN when it is not found. Rule it "
                        "out first: %s."
                        % (rel, key, m.group(1), WHY[kind],
                           SAYS[kind] % key))
    return problems, seen


#Files written to be refused, one reason each, and the shapes that must be
#accepted. `held` is how many of the file's accepting slots come out guarded.
SELF_TESTS = [
    ("an npos lookup on the high side of a <", 0, "accepting side", """
TEST(S, C) { EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("the same lookup, ruled out first", 1, None, """
TEST(S, C) { ASSERT_NE(std::string::npos, w.find("b"));
             EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("a guard naming ANOTHER needle", 0, "accepting side", """
TEST(S, C) { ASSERT_NE(std::string::npos, w.find("msg"));
             EXPECT_LT(w.find("a"), w.find("msg_id")); }
"""),
    ("a lookup bound to a local, guarded through it", 1, None, """
TEST(S, C) { const size_t b = w.find("b");
             ASSERT_NE(std::string::npos, b);
             EXPECT_LT(w.find("a"), b); }
"""),
    ("a lookup bound to a local nothing guards", 0, "accepting side", """
TEST(S, C) { const size_t b = w.find("b"); EXPECT_LT(w.find("a"), b); }
"""),
    ("a rank helper on the LOW side of a <", 0, "accepting side", """
int rankOf(const V &k, const std::string &n)
{ return n.empty()? -1: 0; }
TEST(S, C) { EXPECT_LT(rankOf(k, "a"), rankOf(k, "b")); }
"""),
    ("the same rank helper, ruled out first", 1, None, """
int rankOf(const V &k, const std::string &n)
{ return n.empty()? -1: 0; }
TEST(S, C) { ASSERT_GE(rankOf(k, "a"), 0);
             EXPECT_LT(rankOf(k, "a"), rankOf(k, "b")); }
"""),
    ("a rank helper ruled out by a loop over the needles", 2, None, """
int rankOf(const V &k, const std::string &n)
{ return n.empty()? -1: 0; }
TEST(S, C) { for (const char *n: { "a", "c" }) ASSERT_GE(rankOf(k, n), 0);
             EXPECT_LT(rankOf(k, "a"), rankOf(k, "b"));
             EXPECT_LT(rankOf(k, "c"), rankOf(k, "b")); }
"""),
    ("a guard in a case gtest never runs", 0, "accepting side", """
TEST(S, DISABLED_G) { ASSERT_NE(std::string::npos, w.find("b")); }
TEST(S, C) { EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("a guard the preprocessor drops", 0, "accepting side", """
TEST(S, C) {
#if 0
             ASSERT_NE(std::string::npos, w.find("b"));
#endif
             EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("a guard a failure message only talks about", 0, "accepting side", """
TEST(S, C) { EXPECT_LT(w.find("a"), w.find("b"))
                 << "ASSERT_NE(std::string::npos, w.find(\\"b\\"))"; }
"""),
    ("an npos lookup on the low side, where npos REDDENS", 0, None, """
TEST(S, C) { EXPECT_LT(w.find("a"), limit); }
"""),
    ("a rank helper on the high side, where -1 REDDENS", 0, None, """
int rankOf(const V &k, const std::string &n)
{ return n.empty()? -1: 0; }
TEST(S, C) { EXPECT_LT(first, rankOf(k, "b")); }
"""),
    ("an iterator handed back by a map lookup", 0, None, """
TEST(S, C) { EXPECT_GE(shapes.find(what)->second, 2); }
"""),
    ("a > whose accepting side is the left one", 0, "accepting side", """
TEST(S, C) { EXPECT_GT(w.find("a"), floor); }
"""),
    ("a file-local helper that hands a position back", 0, "accepting side", """
size_t keyPos(const std::string &w, const char *k)
{ return w.find(std::string("\\"") + k + "\\":"); }
TEST(S, C) { EXPECT_LT(keyPos(w, "data"), keyPos(w, "msg_id")); }
"""),
    ("the same helper, ruled out first", 1, None, """
size_t keyPos(const std::string &w, const char *k)
{ return w.find(std::string("\\"") + k + "\\":"); }
TEST(S, C) { ASSERT_NE(std::string::npos, keyPos(w, "msg_id"));
             EXPECT_LT(keyPos(w, "data"), keyPos(w, "msg_id")); }
"""),
    ("a needle carrying an unmatched bracket", 0, "accepting side", """
TEST(S, C) { EXPECT_LT(w.find("a"), w.find("ubyte[")); }
"""),
    ("a guard the compiler never sees, on a line comment", 0, "accepting side", """
TEST(S, C) { //ASSERT_NE(std::string::npos, w.find("b"));
             EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("a guard inside a block comment that spans two lines", 0, "accepting side", """
TEST(S, C) { /* what this case would need is
             ASSERT_NE(std::string::npos, w.find("b")); */
             EXPECT_LT(w.find("a"), w.find("b")); }
"""),
    ("a line comment standing between the guard and the comparison", 1, None, """
TEST(S, C) { ASSERT_NE(std::string::npos, w.find("b"));
             //b is what this case is about
             EXPECT_LT(w.find("a"), w.find("b")); }
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
            bad.append("%s: %d accepting slot(s) guarded, expected %d (%s)"
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
        print("%d ordering assertion(s) able to accept a sentinel, %d "
              "unguarded" % (seen, len(problems)))
        return 1

    print("PASS: %d ordering assertion(s) able to accept a \"not found\" "
          "answer, each ruled out of it by its own case" % seen)
    return 0


if __name__ == '__main__':
    sys.exit(main())
