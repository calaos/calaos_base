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
     BUT the variable alone proves nothing, so before crediting a single file
     to it this check verifies the MECHANISM behind it exists: see
     wholesale_mechanism_problems() and the paragraph below.
  3. otherwise the file must appear, path for path, in a dist-carrying
     variable of some Makefile.am.  The parsing of those variables is *not*
     duplicated here: check-extra-dist.py is imported and its
     load_assignments/expand/DIST_PRIMARY/DIST_OPTIN are reused, so the two
     checks can never disagree on what "declared" means.

BELIEVING A VARIABLE IS HOW THIS CHECK WOULD LIE.  The defect this file exists
to catch is "a declaration says a file ships and nothing actually ships it".
Rule 2 above reproduced that defect inside the checker: neutralise the recipe
(`dist-hook: @true`), leave VENDORED_DIST_TREES untouched, and 219 files stop
reaching the tarball while this check stays green -- measured, in review.  Only
`make distcheck` saw it, and CI runs neither dist nor distcheck.

So rule 2 is now conditional on the mechanism, checked in this order before any
file is credited to a wholesale tree:

  * the Makefile.am that assigns the variable must sit in a directory
    AC_CONFIG_FILES generates a Makefile for -- otherwise automake never reads
    it and no hook of its ever runs;
  * that same Makefile.am must define exactly one `dist-hook` rule;
  * its recipe must mention $(VENDORED_DIST_TREES) and must copy something.

Any of those failing is exit 2 with an explicit message -- an ERROR, not a
silent PASS and not a coverage FAIL, because the checker's own premise is gone.

WHAT REMAINS OUT OF REACH, and it must be said rather than implied: a hook that
mentions the variable and copies *badly* -- wrong destination, a prune that
eats a real file, a `cd` that lands elsewhere -- still satisfies all three.
Statically that is indistinguishable from a correct one.  The consequence, in
plain words: the day someone edits this hook and gets it subtly wrong, nothing
in `make check` will say so; `make distcheck` will, and it is not wired into
CI (F-DIST-2).  Running distcheck after touching the hook is not optional.

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

# The variable a Makefile.am uses to declare "this directory ships whole", and
# the make rule that has to exist for that declaration to mean anything.
WHOLESALE_VAR = 'VENDORED_DIST_TREES'
WHOLESALE_HOOK = 'dist-hook'


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


def rule_recipes(path, target):
    """Every recipe body defined for `target` in one Makefile.am, as strings.

    A make recipe is the run of tab-indented lines following the target line;
    blank and comment lines inside it do not end it, a line starting in column
    one does.  Returning a LIST, not the first match, is deliberate: a second
    `dist-hook:` rule carrying a recipe silently replaces the first one, which
    is a perfectly good way to neutralise the mechanism by addition.
    """
    with open(path, encoding='utf-8', errors='replace') as fh:
        lines = fh.read().split('\n')
    head = re.compile(r'^' + re.escape(target) + r'\s*::?(?:\s|$)')
    out = []
    i = 0
    while i < len(lines):
        if not head.match(lines[i]):
            i += 1
            continue
        # a target line may itself be continued with a trailing backslash
        while lines[i].rstrip().endswith('\\') and i + 1 < len(lines):
            i += 1
        body = []
        j = i + 1
        while j < len(lines):
            line = lines[j]
            if line.startswith('\t'):
                body.append(line)
            elif line.strip() == '' or line.lstrip().startswith('#'):
                k = j + 1
                while k < len(lines) and (lines[k].strip() == ''
                                          or lines[k].lstrip().startswith('#')):
                    k += 1
                if k >= len(lines) or not lines[k].startswith('\t'):
                    break
            else:
                break
            j += 1
        out.append('\n'.join(body))
        i = j
    return out


def wholesale_mechanism_problems(top, makefiles, autodirs, wholesale):
    """Why the WHOLESALE_VAR declaration may not be backed by anything.

    Returns a list of human-readable problems; empty means the variable is
    wired to a hook that at least names it and copies.  See the module
    docstring for what this can and cannot establish.
    """
    problems = []
    if not wholesale:
        return problems          # nothing claims wholesale coverage: nothing to back
    declaring = [mf for mf in makefiles
                 if WHOLESALE_VAR in ced_assign_names(mf)]
    if not declaring:
        problems.append(
            '%s is credited for %d tree(s) but no Makefile.am assigns it'
            % (WHOLESALE_VAR, len(wholesale)))
        return problems
    for mf in declaring:
        rel = os.path.relpath(mf, top)
        if os.path.dirname(mf) not in autodirs:
            problems.append(
                '%s assigns %s but AC_CONFIG_FILES does not generate a Makefile '
                'there -- automake never reads it, so its %s never runs'
                % (rel, WHOLESALE_VAR, WHOLESALE_HOOK))
            continue
        recipes = rule_recipes(mf, WHOLESALE_HOOK)
        if not recipes:
            problems.append(
                '%s assigns %s but defines no %s rule -- the variable is read '
                'by nothing and the trees would not ship'
                % (rel, WHOLESALE_VAR, WHOLESALE_HOOK))
            continue
        if len(recipes) > 1:
            problems.append(
                '%s defines %d %s rules with a recipe; make keeps the last one, '
                'so the earlier one(s) are dead'
                % (rel, len(recipes), WHOLESALE_HOOK))
        body = recipes[-1]
        if ('$(%s)' % WHOLESALE_VAR) not in body and (
                '${%s}' % WHOLESALE_VAR) not in body:
            problems.append(
                '%s: the %s recipe never mentions $(%s) -- the declaration '
                'would be believed and the trees would stay behind'
                % (rel, WHOLESALE_HOOK, WHOLESALE_VAR))
        if not re.search(r'(?:^|[;&|`(\s])(?:cp|install|ln|tar|rsync)\s', body):
            problems.append(
                '%s: the %s recipe copies nothing' % (rel, WHOLESALE_HOOK))
    return problems


def ced_assign_names(mf):
    """Variable names assigned in one Makefile.am -- cheap, parser-free.

    load_assignments() is the authority on VALUES; here only the presence of a
    name matters, and re-reading the file avoids threading its result through.
    """
    names = set()
    try:
        with open(mf, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = re.match(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*[+:?]?=', line)
                if m:
                    names.add(m.group(1))
    except OSError:
        pass
    return names


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

    # Before a single file is credited to a wholesale tree, check the mechanism
    # behind the declaration exists.  A neutralised dist-hook with the variable
    # left intact was measured to hide 219 missing files from this very check.
    problems = wholesale_mechanism_problems(top, makefiles, autodirs,
                                            wholesale)
    for problem in problems:
        print('check-dist-coverage: %s' % problem, file=sys.stderr)
    if problems:
        print('check-dist-coverage: the %s mechanism is broken; refusing to '
              'credit %d tree(s) to it. That premise is what this check runs '
              'on, so it errors out instead of passing.'
              % (WHOLESALE_VAR, len(wholesale)), file=sys.stderr)
        return 2

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
