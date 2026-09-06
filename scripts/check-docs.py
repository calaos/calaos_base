#!/usr/bin/env python3
"""Every `File.cpp:line` of docs/ that carries an anchor must still hit it.

WHAT THIS CLOSES, AND NOTHING MORE.  A line number in prose rots on its own:
the file it points at grows above it and the reference now designates a blank
line or a closing brace.  Nothing reddens, because the file still exists and
the line still exists.  A reference that also quotes a FRAGMENT expected at
that line -- `IO/Scenario.cpp:158` (`if (!sa.io) continue;`) -- becomes
falsifiable, and this is the only question asked here: is the fragment still
there?

WHAT THIS CANNOT DO, said here so that a green run is never read as a correct
documentation:

  * THE PLAUSIBLE REFERENCE THAT LIES.  A line that exists and does contain
    the fragment, while the mechanism described is somewhere else entirely, is
    ACCEPTED -- an anchor would have confirmed it.  Only a reader sees that
    the code quoted is not the code meant.
  * THE FALSE CLAIM THAT CITES NOTHING.  A sentence with no reference has
    nothing to anchor, and is invisible here.

Both classes were measured on a real documentation review: the two defects it
found were one of each, and neither would have been caught below.  What
catches them is the review, and there is no substitute.  This closes the STALE
reference, not the WRONG one.

DELIBERATELY NON-BLOCKING, DELIBERATELY OUT OF `make check`.  A false red on
documentation at every refactoring would end up disarmed, and a check one
disarms is worth less than no check at all.  The exit code below is real -- it
is what the self-test and a reviewer read; the `check-docs` target swallows it
on purpose, and the reason is written next to the target.

SCOPE.  docs/*.md only.  docs/refactoring/ is a pile of dated records: a fiche
quotes the tree as it stood at its own commit and is never rewritten, so its
references are EXPECTED to rot -- measured, 62 of its 72 anchors are already
stale.  Checking them would produce hundreds of reds that are not defects.
"""

import os
import re
import sys

# A citation: an optional elision mark, a path, then one or more :N or :N-M.
# The doc writes `…ProvisioningHandler.cpp:207` when the directory is long;
# the mark is kept because it says the name is a suffix, not a whole path.
REFERENCE = re.compile(
    r'(\u2026|\.\.\.)?'
    r'([A-Za-z0-9_][A-Za-z0-9_./+-]*'
    r'\.(?:cpp|cc|cxx|c|hpp|h|py|lua|sh|am|ac|md|json|xml|txt|in))'
    r'((?::\d+(?:\s*[-\u2013]\s*\d+)?)'
    r'(?:\s*(?:,|et|and|&)?\s*:\d+(?:\s*[-\u2013]\s*\d+)?)*)')

# What may stand between the numbers and the anchor: the closing backtick of
# an inline reference, the target half of a markdown link, and at most one
# line break -- the docs wrap long lines and the anchor lands on the next one.
ANCHOR = re.compile(
    r'`?(?:\]\([^)\s]*\))?[ \t]*\n?[ \t]*\(`([^`\n]{1,160})`\)')

# A continuation: `RemoteUIProvisioningHandler.cpp:56-57`, `:69-76` -- the
# second token names no file and inherits the one before it, which is how the
# docs cite several places of the same source without repeating its path.
CONTINUATION = re.compile(
    r'`((?::\d+(?:\s*[-\u2013]\s*\d+)?)'
    r'(?:\s*(?:,|et|and|&)?\s*:\d+(?:\s*[-\u2013]\s*\d+)?)*)`')

# A bare mention, with no number of its own. The docs open a section with
# "**Fichier :** [JsonApiHandlerWS.cpp](…)" and then cite it by continuation
# alone; without this the continuation would inherit whatever file was named
# last, several sections above, and the probe would accuse an innocent.
MENTION = re.compile(
    r'(\u2026|\.\.\.)?'
    r'([A-Za-z0-9_][A-Za-z0-9_./+-]*'
    r'\.(?:cpp|cc|cxx|c|hpp|h|py|lua|sh|am|ac|md|json|xml|txt|in))(?!:\d)')

LINK = re.compile(r'\[([^\]\n]*)\]\(([^)\s]+)\)')

# Build products and vendored trees: walking them costs seconds and offers
# thousands of ways for a basename to become ambiguous.
PRUNED = {'.git', '_build', 'autom4te.cache', '.libs', '.deps', 'po',
          '__pycache__', 'node_modules', '.serena'}


def squeeze(text):
    """Whitespace-insensitive form: indentation and clang-format must not red."""
    text = re.sub(r'\\([|*_`\[\]])', r'\1', text)
    return re.sub(r'\s+', ' ', text).strip()


def tree_files(top):
    out = []
    for root, dirs, files in os.walk(top):
        dirs[:] = sorted(d for d in dirs if d not in PRUNED)
        rel = os.path.relpath(root, top)
        for name in sorted(files):
            out.append(name if rel == '.' else os.path.join(rel, name))
    return out


def candidates(name, elided, files):
    """Paths a citation may designate; more than one means we do not know."""
    base = os.path.basename(name)
    if elided:
        return [p for p in files if os.path.basename(p).endswith(base)]
    return [p for p in files if p == name or p.endswith('/' + name)]


def numbers(raw):
    return [int(n) for n in re.findall(r'\d+', raw)]


def cited_span(nums):
    """The lines an anchor may live on: the whole range, or the listed lines."""
    if len(nums) == 2 and nums[0] <= nums[1]:
        return list(range(nums[0], nums[1] + 1))
    return sorted(set(nums))


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def scan(top, docs):
    """The single reading shared by the self-test and the real tree."""
    files = tree_files(top)
    body = {}

    def lines_of(path):
        if path not in body:
            full = os.path.join(top, path)
            try:
                with open(full, encoding='utf-8', errors='replace') as fh:
                    body[path] = fh.read().splitlines()
            except OSError:
                body[path] = None
        return body[path]

    problems = []
    stat = {'refs': 0, 'anchored': 0, 'plain': 0, 'unresolved': 0, 'docs': 0}

    for doc in docs:
        with open(os.path.join(top, doc), encoding='utf-8',
                  errors='replace') as fh:
            text = fh.read()
        stat['docs'] += 1
        links = [(m.start(1), m.end(1), m.group(2))
                 for m in LINK.finditer(text)]

        cites = [(m.start(), m.end(), m.group(1), m.group(2), m.group(3))
                 for m in REFERENCE.finditer(text)]
        taken = [(a, b) for a, b, _e, _n, _d in cites]
        for m in MENTION.finditer(text):
            if not any(a <= m.start(2) < b for a, b in taken):
                cites.append((m.start(), m.end(), m.group(1), m.group(2),
                              None))
        for m in CONTINUATION.finditer(text):
            if not any(a <= m.start(1) < b for a, b in taken):
                cites.append((m.start(), m.end(), None, None, m.group(1)))
        cites.sort()

        def target_at(pos):
            for start, end, href in links:
                if start <= pos <= end:
                    return href.split('#')[0]
            return None

        inherited = None
        for start_at, end_at, elided, name, digits in cites:
            if digits is None:
                inherited = (elided, name, target_at(start_at))
                continue
            if name is None:
                if inherited is None:
                    continue
                elided, name, target = inherited
            else:
                inherited = (elided, name, target_at(start_at))
                target = inherited[2]
            stat['refs'] += 1
            where = '%s:%d' % (doc, line_of(text, start_at))
            quoted = '%s%s' % (name, digits)
            tail = ANCHOR.match(text, end_at)
            anchor = tail.group(1) if tail else None
            stat['anchored' if anchor else 'plain'] += 1

            path = None
            if target:
                joined = os.path.normpath(
                    os.path.join(os.path.dirname(doc), target))
                if lines_of(joined) is not None:
                    path = joined
                elif not os.path.isabs(target) and '://' not in target:
                    # The doc names a path of this repository itself, so the
                    # miss is a fact about the tree, not about our guessing.
                    problems.append('%s: `%s` -> %s: no such file'
                                    % (where, quoted, target))
                    continue
            if path is None:
                found = candidates(name, elided, files)
                if len(found) == 1:
                    path = found[0]
            if path is None:
                # Ambiguous basenames and files of other repositories both land
                # here; guessing between them is how a probe accuses innocents.
                stat['unresolved'] += 1
                continue

            source = lines_of(path)
            nums = numbers(digits)
            beyond = [n for n in nums if n < 1 or n > len(source)]
            if beyond:
                problems.append(
                    '%s: `%s` -> %s has %d lines, cited %s'
                    % (where, quoted, path, len(source),
                       ', '.join(str(n) for n in beyond)))
                continue
            if anchor is None:
                continue

            span = cited_span(nums)
            wanted = squeeze(anchor)
            # Joined, so that an anchor straddling two lines of a cited range
            # is still found; a range is quoted as a block, not as lines.
            if wanted in squeeze(' '.join(source[n - 1] for n in span)):
                continue
            elsewhere = [str(n) for n, raw in enumerate(source, 1)
                         if wanted in squeeze(raw)]
            hint = (' -- found at :%s' % ','.join(elsewhere[:3])
                    if elsewhere else ' -- not found in the file')
            problems.append('%s: `%s` (`%s`) -> %s:%d reads "%s"%s'
                            % (where, quoted, anchor, path, span[0],
                               source[span[0] - 1].strip()[:60], hint))

    return problems, stat


# Each entry: label, documents, sources, expected complaint fragments.
# The two ACCEPTED cases at the end are the most important of the table: they
# freeze what this probe cannot do, so that nobody ever presents it as a
# guarantee that the documentation is right.
SELF_TESTS = [
    ("a link naming a file that is not in the tree",
     {'docs/d.md': 'see [gone.cpp:12](../src/gone.cpp).'},
     {}, ['no such file']),
    ("a line past the end of the file",
     {'docs/d.md': 'see `src/a.cpp:99`.'},
     {'src/a.cpp': 'one\ntwo\n'}, ['has 2 lines, cited 99']),
    ("an anchor that drifted three lines down",
     {'docs/d.md': 'see `src/a.cpp:2` (`int answer = 42;`).'},
     {'src/a.cpp': 'a\nb\nc\nd\nint answer = 42;\n'}, ['found at :5']),
    ("an anchor that is nowhere in the file",
     {'docs/d.md': 'see `src/a.cpp:1` (`void vanished()`).'},
     {'src/a.cpp': 'a\nb\n'}, ['not found in the file']),
    ("an anchor absent from the whole cited range",
     {'docs/d.md': 'see `src/a.cpp:1-3` (`hidden()`).'},
     {'src/a.cpp': 'a\nb\nc\nhidden();\n'}, ['found at :4']),
    ("an anchor that only exists above the cited line",
     {'docs/d.md': 'see `src/a.cpp:4` (`moved()`).'},
     {'src/a.cpp': 'moved();\nb\nc\nd\n'}, ['found at :1']),
    ("an anchor written on the line below its reference",
     {'docs/d.md': 'see `src/a.cpp:1`\n(`gone()`).'},
     {'src/a.cpp': 'here();\n'}, ['not found in the file']),
    ("an anchor behind a markdown link",
     {'docs/d.md': 'see [a.cpp:1](../src/a.cpp) (`gone()`).'},
     {'src/a.cpp': 'here();\n'}, ['not found in the file']),
    ("a second number of the list past the end",
     {'docs/d.md': 'see `src/a.cpp:1, :40`.'},
     {'src/a.cpp': 'a\nb\n'}, ['cited 40']),
    ("a full path past the end, next to a file whose basename ends like it",
     {'docs/d.md': 'see `src/a.cpp:99`.'},
     {'src/a.cpp': 'one\ntwo\n', 'src/extra.cpp': 'x\n'}, ['cited 99']),
    ("an elided name past the end, next to the file it is a suffix of",
     {'docs/d.md': 'see `\u2026Handler.cpp:99`.'},
     {'src/RemoteUIHandler.cpp': 'one\n', 'src/other.cpp': 'x\n'},
     ['cited 99']),
    ("a continuation past the end of the file it inherits",
     {'docs/d.md': 'see `src/a.cpp:1` then `:40`.'},
     {'src/a.cpp': 'a\nb\n'}, ['cited 40']),
    ("a continuation whose own anchor drifted",
     {'docs/d.md': '**File:** [a.cpp](../src/a.cpp)\n\nthen `:1` (`b();`).'},
     {'src/a.cpp': 'a();\nb();\n'}, ['found at :2']),
    ("a continuation reaching over the file named between",
     {'docs/d.md': 'see `src/a.cpp:1`, then b.cpp, then `:40`.'},
     {'src/a.cpp': 'a\nb\n', 'src/b.cpp': 'a\nb\n'}, ['cited 40']),

    ("an anchor sitting exactly on its line",
     {'docs/d.md': 'see `src/a.cpp:2` (`int answer = 42;`).'},
     {'src/a.cpp': 'a\nint answer = 42;\n'}, []),
    ("an anchor whose indentation alone differs",
     {'docs/d.md': 'see `src/a.cpp:1` (`if (!sa.io) continue;`).'},
     {'src/a.cpp': '        if (!sa.io)   continue;\n'}, []),
    ("an anchor escaped for a markdown table cell",
     {'docs/d.md': '| x | `src/a.cpp:1` (`a \\| b`) |'},
     {'src/a.cpp': 'return a | b;\n'}, []),
    ("an anchor spread over the two lines of its range",
     {'docs/d.md': 'see `src/a.cpp:1-2` (`long(call, here)`).'},
     {'src/a.cpp': 'long(call,\n here)\n'}, []),
    ("a reference carrying no anchor at all",
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/a.cpp': 'a\n'}, []),
    ("a parenthesis that is prose, not an anchor",
     {'docs/d.md': 'see `src/a.cpp:1` (le decodeur).'},
     {'src/a.cpp': 'a\n'}, []),
    ("a file of another repository",
     {'docs/d.md': 'see `firmware/protocol.h:137`.'}, {'src/a.cpp': 'a\n'},
     []),
    ("a basename two files of the tree answer to",
     {'docs/d.md': 'see `main.cpp:9`.'},
     {'src/main.cpp': 'a\n', 'lib/main.cpp': 'a\n'}, []),
    ("a continuation under the file its section names, not the one above",
     {'docs/d.md': 'see `src/a.cpp:9`.\n\n**File:** [b.cpp](../src/b.cpp)\n\n'
                   'and (`:2`).'},
     {'src/a.cpp': 'a\n' * 9, 'src/b.cpp': 'x\ny\n'}, []),
    ("a continuation in a table cell, under the link of that cell",
     {'docs/d.md': '| [b.cpp](../src/b.cpp) | one (`:2`) |'},
     {'src/a.cpp': 'a\n', 'src/b.cpp': 'x\ny\n'}, []),
    ("an anchor on the very last line",
     {'docs/d.md': 'see `src/a.cpp:2` (`last();`).'},
     {'src/a.cpp': 'a\nlast();'}, []),
    ("a reference that LIES -- the line exists and holds the fragment, the "
     "mechanism described is elsewhere",
     {'docs/d.md': 'the per-IP connection ceiling is `src/a.cpp:1` '
                   '(`return 429;`).'},
     {'src/a.cpp': 'return 429;  // this one is the auth rate limiter\n'}, []),
    ("a false claim citing NOTHING",
     {'docs/d.md': 'nothing is ever dropped from the payload.'},
     {'src/a.cpp': 'if (!sa.io) continue;\n'}, []),
]

# The complaints above say nothing about the SUMMARY, and coverage is what
# this probe exists to grow: counters that have been swapped keep refusing the
# right documents while publishing a progress figure that is false.
COUNTED = (
    {'docs/d.md': '`src/a.cpp:1` (`one();`), `src/a.cpp:2` (`two();`), '
                  '`src/a.cpp:3`, `src/b.cpp:1`, `main.cpp:1`, '
                  '`elsewhere/gone.h:1`.'},
    {'src/a.cpp': 'one();\ntwo();\nthree();\n', 'src/b.cpp': 'x\n',
     'lib/main.cpp': 'x\n', 'src/main.cpp': 'x\n'},
    {'refs': 6, 'anchored': 2, 'plain': 4, 'unresolved': 2, 'docs': 1},
)


def read_once(docs, sources):
    import shutil
    import tempfile

    top = tempfile.mkdtemp(prefix='check-docs-selftest.')
    try:
        for rel, text in list(docs.items()) + list(sources.items()):
            full = os.path.join(top, rel)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', encoding='utf-8') as fh:
                fh.write(text)
        return scan(top, sorted(docs))
    finally:
        shutil.rmtree(top, ignore_errors=True)


def self_test():
    """The reading above, run on documents written to be refused.

    Two probes of this family carry one; the third does not, and for it a
    hand-written mutation is the only way to know it still speaks.  A probe
    whose own reading has broken does not go quiet -- it accuses innocents.
    """
    bad = []
    for label, docs, sources, wanted in SELF_TESTS:
        problems, _stat = read_once(docs, sources)
        said = ' | '.join(problems)
        if not wanted and problems:
            bad.append('%s: accepted nothing, said "%s"' % (label, said))
        elif wanted and not problems:
            bad.append('%s: refused nothing' % label)
        elif wanted and len(problems) != 1:
            bad.append('%s: %d complaints, expected 1 (%s)'
                       % (label, len(problems), said))
        else:
            for fragment in wanted:
                if fragment not in said:
                    bad.append('%s: complaint does not say "%s" (%s)'
                               % (label, fragment, said))

    counted, stat = read_once(COUNTED[0], COUNTED[1])
    if counted:
        bad.append('the counted corpus complained: %s' % ' | '.join(counted))
    for key, want in sorted(COUNTED[2].items()):
        if stat[key] != want:
            bad.append('the counted corpus reports %s = %d, expected %d'
                       % (key, stat[key], want))

    for line in bad:
        print('FAIL: self-test: ' + line)
    if bad:
        return False
    print('PASS: self-test, %d documents written to be refused and %d to be '
          'accepted, each read as meant, plus one corpus whose counters are '
          'pinned' % (sum(1 for t in SELF_TESTS if t[3]),
                      sum(1 for t in SELF_TESTS if not t[3])))
    return True


def main(argv):
    if not self_test():
        return 2
    if '--self-test' in argv:
        return 0

    given = [a for a in argv if not a.startswith('-')]
    top = (given[0] if given else os.environ.get('abs_top_srcdir')
           or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    top = os.path.abspath(top)
    docdir = os.path.join(top, 'docs')
    docs = sorted('docs/' + n for n in os.listdir(docdir)
                  if n.endswith('.md'))

    problems, stat = scan(top, docs)
    for line in problems:
        print('FAIL: ' + line)
    print('check-docs: %d references over %d documents -- %d anchored, '
          '%d unanchored, %d unresolved, %d stale'
          % (stat['refs'], stat['docs'], stat['anchored'], stat['plain'],
             stat['unresolved'], len(problems)))
    print('check-docs: an anchored reference that still hits says nothing '
          'about whether the code quoted is the code meant -- that is the '
          'review, and there is no substitute')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
