#!/usr/bin/env python3
"""T3.45: every file listed in a dist-carrying Makefile.am variable must exist.

`make dist` resolves each entry of EXTRA_DIST -- and of every primary automake
distributes -- as a make target relative to the Makefile.am that declares it.
A path that names no file makes `make dist` die with "No rule to make target",
which is how the release tarball stayed unbuildable from 2025-02-16 to
2026-08-25 without anyone noticing: CI runs neither dist nor distcheck.

This check is the static, sub-second stand-in for that discovery. It walks
every Makefile.am in the tree and verifies each listed path resolves.

Scope, stated so it is not mistaken for a distcheck replacement:

  * covered -- EXTRA_DIST, the primaries automake distributes by default
    (_SOURCES, _HEADERS, _MANS, _TEXINFOS, _LISP, _JAVA, _PYTHON), and the
    opt-in dist_*_{SCRIPTS,DATA}.  nodist_* is excluded: automake does not
    distribute it, and so are BUILT_SOURCES / DIST_SOURCES, which only look
    like primaries -- see NOT_PRIMARY below.
  * covered -- $(srcdir)/, $(top_srcdir)/, aliases of $(srcdir), variables
    defined in the same Makefile.am that expand to a single token, and glob
    patterns (a glob matching nothing is a dead make target, exactly like a
    plain missing path).
  * NOT covered -- $(...) that this file cannot expand (AC_SUBST values,
    $(top_builddir), variables built by += across conditionals): skipped, on
    the principle that a blind spot beats a false red.
  * NOT covered -- the reverse direction "file exists => is it distributed?".
    That is F-DIST-1's territory and needs a real `make dist`.
  * DELIBERATELY STRICTER THAN MAKE -- entries inside `if COND ... endif` are
    checked unconditionally, although automake drops them when COND is false.
    A path that only exists when its own conditional is configured (a
    generated file listed outside nodist_*, say) would therefore be reported
    here while `make dist` stays happy.  The summary line prints how many
    checked paths are conditional so the exposure is visible on every run;
    no such path exists in this tree today.
"""
import fnmatch
import glob
import os
import re
import sys

# Primaries automake distributes unless explicitly opted out with nodist_.
DIST_PRIMARY = re.compile(
    r'^(?!nodist_)[A-Za-z0-9_]*_'
    r'(?:SOURCES|HEADERS|MANS|TEXINFOS|LISP|JAVA|PYTHON)$')
# ...minus the automake special variables that merely end in _SOURCES.
# BUILT_SOURCES names files make GENERATES before `all`; `distdir` depends on
# it to build them, never to ship them (they are absent from DISTFILES), so an
# entry that does not exist in a pristine srcdir is the normal state, not a
# defect -- src/bin/calaos_mcp/Makefile.am's `BUILT_SOURCES += calaos_mcp` is
# exactly that. DIST_SOURCES is automake's own derived list, never hand-written.
NOT_PRIMARY = frozenset(('BUILT_SOURCES', 'DIST_SOURCES'))
# Primaries distributed only when explicitly opted in with dist_.
DIST_OPTIN = re.compile(r'^dist_[A-Za-z0-9_]*_(?:SCRIPTS|DATA)$')

GLOB_CHARS = re.compile(r'[*?\[]')
VAR_REF = re.compile(r'\$\(([A-Za-z0-9_]+)\)')
COND_OPEN = re.compile(r'^\s*if\s+!?[A-Za-z0-9_]+\s*$')
COND_CLOSE = re.compile(r'^\s*endif\b')

SKIP_DIRS = ('.git', '.deps', '.libs', '_build', '_inst', 'autom4te.cache')


def package_glob(top):
    """`$(PACKAGE)-*`: the distdir a failed distcheck leaves inside srcdir.

    Walking it would measure the unpacked copy on top of the real tree
    (25 Makefile.am instead of 13 here), so it is skipped by name.
    """
    try:
        with open(os.path.join(top, 'configure.ac'), encoding='utf-8',
                  errors='replace') as fh:
            m = re.search(r'AC_INIT\(\s*\[?([A-Za-z0-9._+-]+)\]?', fh.read())
        if m:
            return m.group(1) + '-*'
    except OSError:
        pass
    return None


def is_distdir(root, top, name, distdir_glob):
    """True for the `$(PACKAGE)-VERSION` tree distcheck unpacks at top level.

    Anchored at the top directory and confirmed by its own configure.ac, so
    that a real source directory whose name starts with the package name --
    `src/lib/calaos-python` here -- is never mistaken for it.
    """
    return (distdir_glob is not None
            and os.path.abspath(root) == top
            and fnmatch.fnmatch(name, distdir_glob)
            and os.path.exists(os.path.join(root, name, 'configure.ac')))


def load_assignments(path):
    """Return ({var: [tokens]}, {(var, token) that sat inside `if ... endif`}).

    Backslash continuations are joined first, so a path split across lines is
    one token like make sees it.
    """
    with open(path, encoding='utf-8', errors='replace') as fh:
        text = fh.read()
    text = re.sub(r'\\\n', ' ', text)
    out = {}
    conditional = set()
    depth = 0
    for line in text.split('\n'):
        if COND_OPEN.match(line.split('#', 1)[0]):
            depth += 1
            continue
        if COND_CLOSE.match(line.split('#', 1)[0]):
            depth = max(0, depth - 1)
            continue
        line = line.split('#', 1)[0]
        m = re.match(r'^\s*([A-Za-z0-9_]+)\s*\+?=\s*(.*)$', line)
        if m:
            toks = m.group(2).split()
            out.setdefault(m.group(1), []).extend(toks)
            if depth:
                conditional.update((m.group(1), t) for t in toks)
    return out, conditional


def expand(tok, assigns, top, here):
    """Expand what can be expanded; return None when something is left over.

    $(srcdir)/ and $(top_srcdir)/ become real directories; a variable defined
    in this same Makefile.am is inlined when it holds exactly one token.
    """
    tok = tok.replace('$(top_srcdir)', top).replace('${top_srcdir}', top)
    for _ in range(8):
        m = VAR_REF.search(tok)
        if not m:
            return tok
        name = m.group(1)
        if name == 'srcdir':
            value = here
        else:
            value = assigns.get(name)
            if value is None or len(value) != 1:
                return None
            value = value[0]
            if value == '$(srcdir)':
                value = here
        tok = tok[:m.start()] + value + tok[m.end():]
    return None


def main():
    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    top = os.path.abspath(top)
    distdir_glob = package_glob(top)

    makefiles = []
    for root, dirs, files in os.walk(top):
        # Skip VCS and build scratch. _build/_inst are distcheck's own
        # subdirectories, and $(PACKAGE)-* the distdir it unpacks -- all three
        # live inside the srcdir we walk once distcheck has run and failed.
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS
                   and not is_distdir(root, top, d, distdir_glob)]
        if 'Makefile.am' in files:
            makefiles.append(os.path.join(root, 'Makefile.am'))
    makefiles.sort()

    if not makefiles:
        print('check-extra-dist: no Makefile.am found under %s' % top,
              file=sys.stderr)
        return 2

    missing = []
    checked = 0
    conditional_checked = 0
    skipped = 0
    for mf in makefiles:
        here = os.path.dirname(mf)
        assigns, conditional = load_assignments(mf)
        for var, tokens in assigns.items():
            if var in NOT_PRIMARY:
                continue
            if not (var == 'EXTRA_DIST' or DIST_PRIMARY.match(var)
                    or DIST_OPTIN.match(var)):
                continue
            for tok in tokens:
                resolved = expand(tok, assigns, top, here)
                if resolved is None:
                    skipped += 1
                    continue
                checked += 1
                if (var, tok) in conditional:
                    conditional_checked += 1
                full = resolved if os.path.isabs(resolved) else os.path.join(
                    here, resolved)
                if GLOB_CHARS.search(resolved):
                    # A glob matching nothing stays literal and make dies on
                    # it exactly as it does on a plain missing path.
                    ok = bool(glob.glob(full))
                else:
                    ok = os.path.exists(full)
                if not ok:
                    missing.append((os.path.relpath(mf, top), var, tok))

    for mf, var, tok in missing:
        print('%s: %s lists a path that does not exist: %s' % (mf, var, tok),
              file=sys.stderr)

    print('check-extra-dist: %d Makefile.am, %d dist paths checked '
          '(%d inside if/endif), %d unexpandable skipped, %d missing'
          % (len(makefiles), checked, conditional_checked, skipped,
             len(missing)))
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
