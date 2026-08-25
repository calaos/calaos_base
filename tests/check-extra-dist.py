#!/usr/bin/env python3
"""T3.45: every file listed in a dist-carrying Makefile.am variable must exist.

`make dist` resolves each entry of EXTRA_DIST (and the dist_*/_PYTHON family)
as a make target relative to the Makefile.am that declares it. A path that
names no file makes `make dist` die with "No rule to make target", which is
how the release tarball stayed unbuildable from 2025-02-16 to 2026-08-25
without anyone noticing: CI never runs dist or distcheck.

This check is the static, sub-second stand-in for that discovery. It walks
every Makefile.am in the tree and verifies each listed path resolves.
"""
import os
import re
import sys

# Variables whose entries automake turns into distdir targets.
DIST_VAR = re.compile(
    r'^(EXTRA_DIST|dist_[A-Za-z0-9_]*(?:SCRIPTS|DATA|HEADERS|SOURCES)'
    r'|[A-Za-z0-9_]*_PYTHON)$'
)

# Entries we cannot resolve statically and must not guess at.
UNRESOLVABLE = re.compile(r'[*?\[\]]|\$\((?!srcdir\b)')


def load_assignments(path):
    """Return {var: [tokens]} for a Makefile.am, joining backslash continuations."""
    with open(path, encoding='utf-8', errors='replace') as fh:
        text = fh.read()
    text = re.sub(r'\\\n', ' ', text)
    out = {}
    for line in text.split('\n'):
        line = line.split('#', 1)[0]
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*\+?=\s*(.*)$', line)
        if m:
            out.setdefault(m.group(1), []).extend(m.group(2).split())
    return out


def simple_vars(assigns):
    """Vars that resolve to plain $(srcdir), e.g. `pythonsrcdir = $(srcdir)`."""
    return {k for k, v in assigns.items() if v == ['$(srcdir)']}


def main():
    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    top = os.path.abspath(top)

    makefiles = []
    for root, dirs, files in os.walk(top):
        # Skip VCS and build scratch. _build/_inst are distcheck's own
        # subdirectories, which live inside the unpacked srcdir we walk.
        dirs[:] = [d for d in dirs if d not in (
            '.git', '.deps', '.libs', '_build', '_inst', 'autom4te.cache')]
        if 'Makefile.am' in files:
            makefiles.append(os.path.join(root, 'Makefile.am'))
    makefiles.sort()

    if not makefiles:
        print('check-extra-dist: no Makefile.am found under %s' % top,
              file=sys.stderr)
        return 2

    missing = []
    checked = 0
    for mf in makefiles:
        here = os.path.dirname(mf)
        assigns = load_assignments(mf)
        srcdir_aliases = simple_vars(assigns)
        for var, tokens in assigns.items():
            if not DIST_VAR.match(var):
                continue
            for tok in tokens:
                # Strip a leading $(srcdir)/ or an alias of it.
                stripped = tok
                for alias in ['srcdir'] + sorted(srcdir_aliases):
                    prefix = '$(%s)/' % alias
                    if stripped.startswith(prefix):
                        stripped = stripped[len(prefix):]
                        break
                if UNRESOLVABLE.search(stripped):
                    continue
                checked += 1
                if not os.path.exists(os.path.join(here, stripped)):
                    missing.append((os.path.relpath(mf, top), var, tok))

    for mf, var, tok in missing:
        print('%s: %s lists a path that does not exist: %s' % (mf, var, tok),
              file=sys.stderr)

    print('check-extra-dist: %d Makefile.am, %d dist paths checked, %d missing'
          % (len(makefiles), checked, len(missing)))
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
