#!/usr/bin/env python3
"""T3.48: every source file under src/ and tests/ must reach the tarball.

This is the *reverse* of tests/check-extra-dist.py and the two are meant to be
read together:

    check-extra-dist.py   "declared => exists"   a dist path naming no file
                                                 kills `make dist` (T3.45)
    check-dist-coverage.py "exists => declared"  a file naming no dist entry
                                                 silently ships a broken
                                                 tarball (T3.48 / F-DIST-1)

The second direction is the one that made the release archive unbuildable:
`make dist` succeeded, produced calaos-git.tar.gz, and 417 tracked files -- 126
of them C/C++ sources and headers -- were simply not in it.  src/lib/Makefile.am
shipped the umbrella header uvw/src/uvw.hpp and none of the 27 headers it
includes; exprtk.hpp was listed nowhere at all; the licences of four vendored
libraries never left the repository.  Unpacking the tarball and building it
failed on `fatal error: exprtk.hpp: No such file or directory`.

`make distcheck` is the real oracle for that, and it is not usable here: it
reconfigures and rebuilds the whole tree twice, out of tree, and costs minutes.
`make check` must stay in the seconds.  So this file answers the same question
statically, in about the time python3 takes to start.

HOW COVERAGE IS DECIDED, in order:

  1. build artefacts are not source and are skipped -- pruned directories,
     object/library/log extensions, `Makefile`/`Makefile.in`, anything named by
     a *_PROGRAMS / *_LTLIBRARIES / *_SCRIPTS / *CLEANFILES / BUILT_SOURCES /
     nodist_* variable, and any file X sitting next to an X.in (configure
     output).  The exclusion is *derived from the Makefile.am themselves*, so
     adding a program or a generated file needs no edit here.
  2. a file inside a directory named by VENDORED_DIST_TREES is covered: those
     trees are copied whole by the dist-hook of the Makefile.am that declares
     the variable.  This is what keeps the check from rotting -- a `git subtree
     pull` that adds 40 files to src/lib/uvw needs no change anywhere.
  3. otherwise the file must appear, path for path, in a dist-carrying
     variable of some Makefile.am.  The parsing of those variables is *not*
     duplicated here: check-extra-dist.py is imported and its
     load_assignments/expand/DIST_PRIMARY/DIST_OPTIN are reused, so the two
     checks can never disagree on what "declared" means.

There is NO allow-list of tolerated exceptions, on purpose: "every file under
src/ and tests/ ships" is an invariant that stays true by itself, where a list
of forgiven paths would quietly grow.  docs/, data/ and the dotfile directories
are outside the scanned scope -- not distributing docs/ is a deliberate choice
(162 files), and saying so here is cheaper than forgiving them one by one.

WHAT IT DOES NOT SEE -- the same honest list check-extra-dist.py keeps:

  * a distributed file that the build nevertheless cannot use (wrong -I path,
    a generated file missing from the tarball): distcheck territory.
  * anything outside src/ and tests/.
  * $(...) tokens check-extra-dist.py cannot expand: they are skipped there and
    therefore missing from `declared` here, which can only make this check
    stricter, never laxer.
  * it walks the filesystem, not git.  Run inside an unpacked tarball it sees
    only the files that shipped, so it is vacuously green there -- the place it
    means something is a checkout.
"""
import importlib.util
import os
import re
import sys

# Scanned scope: the two directory trees whose contents must all reach the
# tarball. data/ and docs/ are deliberately out (see module docstring).
SCOPE = ('src', 'tests')

# Directories that never hold distributable source.  '.deps'/'.libs' and the
# distcheck scratch dirs matter because this check must give the same answer in
# a freshly cloned tree and in a fully built one -- that difference is exactly
# what hid F-DIST-1's real size for so long.
PRUNE_DIRS = frozenset((
    '.git', '.deps', '.libs', '_build', '_inst', 'autom4te.cache',
    '__pycache__', 'node_modules', '.pytest_cache',
))

# Build droppings identified by name or extension.
SKIP_EXTS = frozenset((
    '.o', '.lo', '.la', '.a', '.so', '.Po', '.Plo', '.pyc', '.pyo',
    '.gcno', '.gcda', '.gcov', '.log', '.trs', '.orig', '.rej', '.swp',
))
SKIP_NAMES = frozenset((
    'Makefile', '.dirstamp', 'config.h', 'stamp-h1', 'test-suite.log',
))

# Files automake puts in DIST_COMMON on its own, without any variable naming
# them -- but ONLY in a directory for which configure generates a Makefile.
# src/lib/libquickmail/Makefile.am is the reason for that restriction: it is an
# upstream leftover, absent from AC_CONFIG_FILES, and automake ships nothing
# from there.  Measured on automake 1.16.5: src/lib/calaos-python/README.md and
# data/debug/README.md reach the tarball although no variable lists them.
AUTO_DIST_NAMES = frozenset((
    'Makefile.am', 'Makefile.in',
    'ABOUT-GNU', 'ABOUT-NLS', 'AUTHORS', 'BACKLOG', 'COPYING', 'COPYING.DOC',
    'COPYING.LESSER', 'COPYING.LIB', 'ChangeLog', 'INSTALL', 'NEWS',
    'README', 'README.md', 'README.rst', 'README.txt', 'README-alpha',
    'THANKS', 'TODO',
))

# Variables whose entries name things make BUILDS or REMOVES, never ships.
# Deriving the exclusion from these instead of hard-coding binary names is what
# lets a new check_PROGRAMS entry appear without touching this file.
BUILD_OUTPUT_VARS = re.compile(
    r'^(?:nodist_[A-Za-z0-9_]*|'
    r'[A-Za-z0-9_]*_(?:PROGRAMS|LTLIBRARIES|LIBRARIES|SCRIPTS)|'
    r'BUILT_SOURCES|CLEANFILES|MOSTLYCLEANFILES|DISTCLEANFILES|'
    r'MAINTAINERCLEANFILES)$')

# The variable a Makefile.am uses to declare "this directory ships whole".
WHOLESALE_VAR = 'VENDORED_DIST_TREES'


def load_extra_dist_module(top):
    """Import tests/check-extra-dist.py -- the dash forbids a plain import.

    Reusing its parser rather than writing a second one is deliberate: the two
    checks are opposite directions of one question, and a divergence in how
    they read a Makefile.am would make one of them lie.
    """
    path = os.path.join(top, 'tests', 'check-extra-dist.py')
    spec = importlib.util.spec_from_file_location('check_extra_dist', path)
    if spec is None or spec.loader is None:
        raise ImportError(path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def configured_makefile_dirs(top):
    """Directories AC_CONFIG_FILES asks configure to generate a Makefile in.

    Those, and only those, get automake's DIST_COMMON treatment.
    """
    out = set()
    try:
        with open(os.path.join(top, 'configure.ac'), encoding='utf-8',
                  errors='replace') as fh:
            text = fh.read()
    except OSError:
        return out
    for block in re.findall(r'AC_CONFIG_FILES\(\s*\[([^]]*)\]', text):
        for entry in block.split():
            entry = entry.strip()
            if entry == 'Makefile' or entry.endswith('/Makefile'):
                out.add(os.path.normpath(
                    os.path.join(top, os.path.dirname(entry))))
    return out


def find_makefile_ams(top, ced):
    out = []
    for root, dirs, files in os.walk(top):
        dirs[:] = [d for d in dirs
                   if d not in PRUNE_DIRS and d not in ced.SKIP_DIRS
                   and not ced.is_distdir(root, top, d, ced.package_glob(top))]
        if 'Makefile.am' in files:
            out.append(os.path.join(root, 'Makefile.am'))
    out.sort()
    return out


def collect(top, ced, makefiles, autodirs):
    """Return (declared paths, wholesale dirs, build outputs), all absolute.

    Only Makefile.am in directories configure actually generates a Makefile for
    are credited: src/lib/libquickmail/Makefile.am is upstream leftover, absent
    from AC_CONFIG_FILES, and make never reads it -- its dist_man_MANS ships
    nothing.  Counting it would have credited three files that the measured
    tarball does not contain.
    """
    declared, wholesale, outputs = set(), set(), set()
    used = 0
    for mf in makefiles:
        here = os.path.dirname(mf)
        if here not in autodirs:
            continue
        used += 1
        assigns, _cond = ced.load_assignments(mf)
        for var, tokens in assigns.items():
            if var in ced.NOT_PRIMARY:
                target = None
            elif var == WHOLESALE_VAR:
                target = wholesale
            elif (var == 'EXTRA_DIST' or ced.DIST_PRIMARY.match(var)
                    or ced.DIST_OPTIN.match(var)):
                target = declared
            elif BUILD_OUTPUT_VARS.match(var):
                target = outputs
            else:
                target = None
            if target is None:
                continue
            for tok in tokens:
                resolved = ced.expand(tok, assigns, top, here)
                if resolved is None:
                    continue
                full = resolved if os.path.isabs(resolved) else os.path.join(
                    here, resolved)
                full = os.path.normpath(full)
                if ced.GLOB_CHARS.search(resolved):
                    import glob as _glob
                    target.update(os.path.normpath(p)
                                  for p in _glob.glob(full))
                else:
                    target.add(full)
    return declared, wholesale, outputs, used


def under(path, dirs):
    for d in dirs:
        if path == d or path.startswith(d + os.sep):
            return True
    return False


def main():
    top = os.environ.get('abs_top_srcdir') or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..')
    top = os.path.abspath(top)

    try:
        ced = load_extra_dist_module(top)
    except (ImportError, OSError) as exc:
        print('check-dist-coverage: cannot load check-extra-dist.py: %s' % exc,
              file=sys.stderr)
        return 2

    makefiles = find_makefile_ams(top, ced)
    if not makefiles:
        print('check-dist-coverage: no Makefile.am found under %s' % top,
              file=sys.stderr)
        return 2

    autodirs = configured_makefile_dirs(top)
    declared, wholesale, outputs, used = collect(top, ced, makefiles, autodirs)

    uncovered = []
    scanned = 0
    covered_wholesale = 0
    scopes = 0
    for scope in SCOPE:
        base = os.path.join(top, scope)
        if not os.path.isdir(base):
            continue
        scopes += 1
        for root, dirs, files in os.walk(base):
            dirs[:] = [d for d in dirs if d not in PRUNE_DIRS]
            for name in sorted(files):
                if name in SKIP_NAMES:
                    continue
                if os.path.splitext(name)[1] in SKIP_EXTS:
                    continue
                full = os.path.normpath(os.path.join(root, name))
                if name in AUTO_DIST_NAMES and os.path.normpath(root) in \
                        autodirs:
                    continue
                if full in outputs:
                    continue
                # X generated from X.in by config.status: the .in ships, X does
                # not, and in a built tree X exists. Skipping it by shape keeps
                # the answer identical in a clean and in a built checkout.
                if os.path.exists(full + '.in'):
                    continue
                scanned += 1
                if under(full, wholesale):
                    covered_wholesale += 1
                    continue
                if full not in declared:
                    uncovered.append(os.path.relpath(full, top))

    for path in uncovered:
        print('%s: not named by any dist variable and not inside a '
              '%s tree -- it will be missing from the tarball'
              % (path, WHOLESALE_VAR), file=sys.stderr)

    print('check-dist-coverage: %d/%d Makefile.am configured, %d files '
          'scanned under %s, %d covered by %d %s, %d uncovered'
          % (used, len(makefiles), scanned, '/'.join(SCOPE),
             covered_wholesale, len(wholesale), WHOLESALE_VAR,
             len(uncovered)))
    if scopes == 0:
        print('check-dist-coverage: none of %s exists under %s'
              % ('/'.join(SCOPE), top), file=sys.stderr)
        return 2
    return 1 if uncovered else 0


if __name__ == '__main__':
    sys.exit(main())
