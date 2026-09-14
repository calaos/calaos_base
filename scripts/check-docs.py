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

THE PIN FILE IS WHAT MAKES THE OTHER 1451 FALSIFIABLE.  An anchor written in
the prose is optional and the tree carries six; a reference without one can
only be checked for the line EXISTING, so any diff above it makes it lie in
silence.  `scripts/check-docs.pins` records, for every reference that resolves,
a digest of the lines it designates today.  A line shift then changes the
digest and reddens, an edit that leaves the cited lines alone does not, and a
reference newly written into the docs is refused until it is pinned.

WHAT A PIN IS NOT.  It says what a reference designates, never that it
designates the right thing -- regenerating the file after a refactoring is
exactly the gesture that once carried a wrong target forward with fidelity.
The red is a TRIGGER for the reader of the diff; `--write-pins` is the reader's
answer, not a way to make the red go away.

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

import hashlib
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


# The pin file: one TAB-separated record per resolved reference, sorted, so a
# regeneration produces a diff a human reads line by line.  The key is the
# document, the citation as written, and which occurrence of it this is -- NOT
# the line the citation sits on, which every edit above it would shift and
# would turn a doc reword into hundreds of false reds.
PIN_NAME = 'scripts/check-docs.pins'
PIN_HEAD = ('# Generated by scripts/check-docs.py --write-pins.  What each\n'
            '# reference designated when it was pinned; a digest that no\n'
            '# longer matches is a line shift, not a verdict on the prose.\n'
            '# document\tnth\tcitation\tdigest\tfirst 60 columns\n')
PIN_WIDTH = 60


def pin_of(text):
    """Digest plus a readable head; the digest decides, the head explains."""
    return hashlib.sha1(text.encode('utf-8')).hexdigest()[:12], text[:PIN_WIDTH]


def load_pins(path):
    """Pins, or the reason there are none -- never an empty dict in silence.

    A probe that shrugs when its expectation file is absent measures nothing
    while still printing a summary, so the caller is handed an error to print
    and no pins at all.
    """
    try:
        with open(path, encoding='utf-8') as fh:
            raw = fh.read()
    except OSError as exc:
        return None, 'cannot read %s: %s' % (PIN_NAME, exc.strerror)
    pins = {}
    for lineno, line in enumerate(raw.splitlines(), 1):
        if not line or line.startswith('#'):
            continue
        parts = line.split('\t')
        if len(parts) != 5:
            return None, '%s:%d: %d fields, expected 5' % (PIN_NAME, lineno,
                                                           len(parts))
        doc, nth, quoted, digest, head = parts
        key = (doc, int(nth), quoted)
        if key in pins:
            return None, '%s:%d: `%s` pinned twice' % (PIN_NAME, lineno, quoted)
        pins[key] = (digest, head)
    return pins, None


def write_pins(path, records):
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write(PIN_HEAD)
        for doc, nth, quoted, digest, head in sorted(records):
            fh.write('%s\t%d\t%s\t%s\t%s\n'
                     % (doc, nth, quoted, digest, head))


def scan(top, docs, pins=None, records=None):
    """The single reading shared by the self-test and the real tree.

    `pins` given means every resolved reference must be pinned and must still
    hit its pin; `records` given collects what a regeneration would write.
    """
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
    stat = {'refs': 0, 'anchored': 0, 'plain': 0, 'unresolved': 0, 'docs': 0,
            'pinned': 0, 'unpinned': 0, 'drifted': 0}
    seen_pins = set()

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
        nth_of = {}
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
            # Counted here, before any refusal below: a reference the probe
            # gave up on is still CITED, and reporting its pin as orphaned on
            # top of the real complaint would bill one defect twice.
            nth_of[quoted] = nth_of.get(quoted, 0) + 1
            key = (doc, nth_of[quoted], quoted)
            seen_pins.add(key)

            nums = numbers(digits)
            beyond = [n for n in nums if n < 1 or n > len(source)]
            if beyond:
                problems.append(
                    '%s: `%s` -> %s has %d lines, cited %s'
                    % (where, quoted, path, len(source),
                       ', '.join(str(n) for n in beyond)))
                continue

            span = cited_span(nums)
            designated = squeeze(' '.join(source[n - 1] for n in span))
            digest, head = pin_of(designated)
            if records is not None:
                records.append((doc, key[1], quoted, digest, head))
            if pins is not None:
                if key not in pins:
                    stat['unpinned'] += 1
                    problems.append(
                        '%s: `%s` -> %s is not pinned; `--write-pins` after '
                        'reading what it designates' % (where, quoted, path))
                elif pins[key][0] != digest:
                    stat['drifted'] += 1
                    problems.append(
                        '%s: `%s` -> %s:%d was pinned on "%s", now reads "%s"'
                        % (where, quoted, path, span[0], pins[key][1], head))
                else:
                    stat['pinned'] += 1

            if anchor is None:
                continue

            wanted = squeeze(anchor)
            # Joined, so that an anchor straddling two lines of a cited range
            # is still found; a range is quoted as a block, not as lines.
            if wanted in designated:
                continue
            elsewhere = [str(n) for n, raw in enumerate(source, 1)
                         if wanted in squeeze(raw)]
            hint = (' -- found at :%s' % ','.join(elsewhere[:3])
                    if elsewhere else ' -- not found in the file')
            problems.append('%s: `%s` (`%s`) -> %s:%d reads "%s"%s'
                            % (where, quoted, anchor, path, span[0],
                               source[span[0] - 1].strip()[:60], hint))

    if pins is not None:
        # A pin left behind by a reference that no longer exists would let the
        # file rot into a record of a tree that is gone.  Restricted to the
        # documents actually read: a release tarball carries one of the
        # eighteen, and accusing the seventeen absent ones of losing their
        # references would be a red about the caller's argument list.
        scanned = set(docs)
        orphans = [k for k in set(pins) - seen_pins if k[0] in scanned]
        for doc, nth, quoted in sorted(orphans):
            problems.append('%s: `%s` (occurrence %d) is pinned but no longer '
                            'cited there' % (doc, quoted, nth))

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


# The pin corpora. Each entry pins ONE tree and reads ANOTHER against those
# pins, because the only question a pin answers is what a later edit did to the
# lines it recorded -- a corpus that pins and re-reads the same bytes can only
# ever say yes.  Both sides are written out in full, docs and sources, so that
# which side moved is never a guess.  The three ACCEPTED cases at the end are
# what keeps the mechanism usable: a probe reddening at every edit of a cited
# file would be turned off within weeks.
# Each entry: label, pinned docs, pinned sources, read docs, read sources,
# expected complaint fragments.
PIN_TESTS = [
    ("lines inserted above a reference that carries no anchor at all",
     {'docs/d.md': 'see `src/a.cpp:3`.'}, {'src/a.cpp': 'a\nb\ntarget();\n'},
     {'docs/d.md': 'see `src/a.cpp:3`.'},
     {'src/a.cpp': 'new\nlines\na\nb\ntarget();\n'},
     ['was pinned on "target();", now reads "a"']),
    ("a comment squeezed from three lines to one ABOVE the reference",
     {'docs/d.md': 'see `src/a.cpp:5`.'},
     {'src/a.cpp': '// one\n// two\n// three\n\ntarget();\ntail\nend\n'},
     {'docs/d.md': 'see `src/a.cpp:5`.'},
     {'src/a.cpp': '// one two three\n\ntarget();\ntail\nend\n'},
     ['was pinned on "target();", now reads "end"']),
    ("a cited RANGE whose contents changed while its length did not",
     {'docs/d.md': 'see `src/a.cpp:1-2`.'},
     {'src/a.cpp': 'first();\nsecond();\n'},
     {'docs/d.md': 'see `src/a.cpp:1-2`.'},
     {'src/a.cpp': 'first();\nreplaced();\n'},
     ['was pinned on "first(); second();"']),
    ("a reference written into the docs and never pinned",
     {'docs/d.md': 'nothing is cited here.'}, {'src/a.cpp': 'a\n'},
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/a.cpp': 'a\n'},
     ['is not pinned']),
    ("a pin whose reference has been deleted from the document",
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/a.cpp': 'a\n'},
     {'docs/d.md': 'the sentence lost its reference.'}, {'src/a.cpp': 'a\n'},
     ['`src/a.cpp:1` (occurrence 1) is pinned but no longer cited there']),
    ("the SECOND occurrence of one citation dropped, the first standing",
     {'docs/d.md': 'see `src/a.cpp:1` and, later, `src/a.cpp:1`.'},
     {'src/a.cpp': 'kept();\n'},
     {'docs/d.md': 'see `src/a.cpp:1` only.'}, {'src/a.cpp': 'kept();\n'},
     ['`src/a.cpp:1` (occurrence 2) is pinned but no longer cited there']),
    ("a reference whose file was renamed away under it",
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/a.cpp': 'target();\n'},
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/b.cpp': 'target();\n'},
     ['is pinned but no longer cited there']),

    ("a cited file edited BELOW every line it is cited at",
     {'docs/d.md': 'see `src/a.cpp:1`.'},
     {'src/a.cpp': 'target();\ntail\n'},
     {'docs/d.md': 'see `src/a.cpp:1`.'},
     {'src/a.cpp': 'target();\nrewritten\nand longer\n'}, []),
    ("a cited line reindented and nothing else",
     {'docs/d.md': 'see `src/a.cpp:1`.'},
     {'src/a.cpp': 'if (!sa.io) continue;\n'},
     {'docs/d.md': 'see `src/a.cpp:1`.'},
     {'src/a.cpp': '        if (!sa.io)   continue;\n'}, []),
    ("a SECOND document left out of the reading, its pins still in the file",
     {'docs/d.md': 'see `src/a.cpp:1`.', 'docs/e.md': 'also `src/a.cpp:1`.'},
     {'src/a.cpp': 'target();\n'},
     {'docs/d.md': 'see `src/a.cpp:1`.'}, {'src/a.cpp': 'target();\n'}, []),
    ("a DOCUMENT reworded above its reference, the reference itself untouched",
     {'docs/d.md': 'a paragraph.\n\nsee `src/a.cpp:1`.'},
     {'src/a.cpp': 'target();\n'},
     {'docs/d.md': 'a paragraph.\n\nand a second one.\n\nmuch later, see '
                   '`src/a.cpp:1`.'},
     {'src/a.cpp': 'target();\n'}, []),
]


def read_once(docs, sources, pins=None, records=None):
    import shutil
    import tempfile

    top = tempfile.mkdtemp(prefix='check-docs-selftest.')
    try:
        for rel, text in list(docs.items()) + list(sources.items()):
            full = os.path.join(top, rel)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', encoding='utf-8') as fh:
                fh.write(text)
        return scan(top, sorted(docs), pins=pins, records=records)
    finally:
        shutil.rmtree(top, ignore_errors=True)


def wiring_test():
    """What `sweep()` does with the pin file -- the part no corpus above sees.

    The corpora hand `scan()` its pins directly, so none of them says whether
    the real run FINDS the file, RAISES when it is absent, or passes what it
    read down to the reading.  A probe that answers "no pins, nothing to say"
    prints its whole summary and is indistinguishable from a green tree.
    """
    import shutil
    import tempfile

    bad = []
    top = tempfile.mkdtemp(prefix='check-docs-wiring.')
    try:
        os.makedirs(os.path.join(top, 'docs'))
        os.makedirs(os.path.join(top, 'src'))
        os.makedirs(os.path.join(top, os.path.dirname(PIN_NAME)))
        with open(os.path.join(top, 'docs/d.md'), 'w') as fh:
            fh.write('see `src/a.cpp:2`.\n')
        with open(os.path.join(top, 'src/a.cpp'), 'w') as fh:
            fh.write('a\ntarget();\n')
        docs = ['docs/d.md']

        rc, said = sweep(top, docs)
        if rc != 2 or not any('nothing was checked' in l for l in said):
            bad.append('no pin file at all: sweep() said rc=%d, %r -- it must '
                       'refuse, not summarise' % (rc, said))

        rc, said = sweep(top, docs, do_write=True)
        if rc != 0 or not os.path.exists(os.path.join(top, PIN_NAME)):
            bad.append('--write-pins wrote no file (rc=%d)' % rc)

        rc, said = sweep(top, docs)
        if rc != 0 or any(l.startswith('FAIL') for l in said):
            bad.append('the tree it had just pinned was refused: %r' % said)

        with open(os.path.join(top, 'src/a.cpp'), 'w') as fh:
            fh.write('inserted\na\ntarget();\n')
        rc, said = sweep(top, docs)
        if rc != 1 or not any('was pinned on "target();"' in l for l in said):
            bad.append('a line shift after pinning went unreported: rc=%d, %r'
                       % (rc, said))

        with open(os.path.join(top, PIN_NAME), 'a') as fh:
            fh.write('docs/d.md\t1\tbroken\n')
        rc, said = sweep(top, docs)
        if rc != 2 or not any('expected 5' in l for l in said):
            bad.append('a malformed pin file was read anyway: rc=%d, %r'
                       % (rc, said))
    finally:
        shutil.rmtree(top, ignore_errors=True)
    return ['wiring: ' + line for line in bad]


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

    for label, pdocs, psrc, rdocs, rsrc, wanted in PIN_TESTS:
        records = []
        pinned, _stat = read_once(pdocs, psrc, records=records)
        if pinned:
            bad.append('%s: the pinned tree already complained: %s'
                       % (label, ' | '.join(pinned)))
            continue
        pins = {(d, n, q): (dg, hd) for d, n, q, dg, hd in records}
        if not wanted and not pins:
            # An ACCEPTED case that pinned nothing would be green because there
            # was nothing to ask, which is the shape this whole file exists to
            # refuse.  A refused case is vouched for by its own complaint.
            bad.append('%s: pinned nothing, so it accepts anything' % label)
        problems, _stat = read_once(rdocs, rsrc, pins=pins)
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

    bad.extend(wiring_test())

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
          'pinned, plus %d pinned trees re-read after an edit (%d refused, '
          '%d accepted), plus five passes through `sweep()` itself -- the '
          'pin file absent, written, honoured, drifted, and malformed'
          % (sum(1 for t in SELF_TESTS if t[3]),
             sum(1 for t in SELF_TESTS if not t[3]), len(PIN_TESTS),
             sum(1 for t in PIN_TESTS if t[5]),
             sum(1 for t in PIN_TESTS if not t[5])))
    return True


def sweep(top, docs, do_write=False):
    """Everything main() does once the self-test has passed, as a value.

    Factored out because the hole already measured on this very probe
    (`F-DOCS-1`) is that its corpora call `scan()` with the documents in
    ARGUMENT: how the real run finds its input, and what it does when the pin
    file is missing, were exercised by nothing.  The self-test calls this.
    """
    said = []
    pin_path = os.path.join(top, PIN_NAME)
    if do_write:
        records = []
        problems, _stat = scan(top, docs, records=records)
        write_pins(pin_path, records)
        said.append('check-docs: %d references pinned into %s; read the diff '
                    'before committing it -- a pin recalculated without '
                    'looking carries a wrong target forward with fidelity'
                    % (len(records), PIN_NAME))
        return (1 if problems else 0), said

    pins, why = load_pins(pin_path)
    if pins is None:
        # No fallback to "no pins": that would leave the summary printing while
        # the only falsifiable question went unasked, which is this probe's own
        # recorded way of going quiet without being wrong once.
        said.append('FAIL: ' + why)
        said.append('check-docs: nothing was checked -- `--write-pins` builds '
                    'the file, and it belongs in the commit')
        return 2, said

    problems, stat = scan(top, docs, pins=pins)
    for line in problems:
        said.append('FAIL: ' + line)
    said.append('check-docs: %d references over %d documents -- %d anchored, '
                '%d unanchored, %d unresolved, %d stale'
                % (stat['refs'], stat['docs'], stat['anchored'], stat['plain'],
                   stat['unresolved'], len(problems)))
    said.append('check-docs: %d pinned and still on their lines, %d not '
                'pinned, %d drifted off their pin'
                % (stat['pinned'], stat['unpinned'], stat['drifted']))
    said.append('check-docs: an anchored reference that still hits says '
                'nothing about whether the code quoted is the code meant -- '
                'that is the review, and there is no substitute')
    return (1 if problems else 0), said


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

    rc, said = sweep(top, docs, do_write='--write-pins' in argv)
    for line in said:
        print(line)
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
