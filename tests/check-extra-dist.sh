#!/bin/sh
# T3.45 -- every path listed in a dist-carrying Makefile.am variable must exist.
#
# Why this exists: `make dist` turns each EXTRA_DIST entry into a make target
# relative to the Makefile.am that declares it. An entry naming no file kills
# the tarball build with "No rule to make target". That is exactly what
# src/lib/calaos-python/Makefile.am did from 2025-02-16 until T3.45, and no
# one saw it because CI runs neither dist nor distcheck.
#
# `make distcheck` is the real oracle but costs minutes (it rebuilds the whole
# tree twice, out of tree, against a read-only srcdir). This static check is
# its sub-second stand-in for the one failure mode that actually bit us: a
# dist path that does not resolve. It covers EXTRA_DIST *and* every primary
# automake distributes by default (_SOURCES, _HEADERS, _MANS, _TEXINFOS,
# _LISP, _JAVA, _PYTHON) plus dist_*_{SCRIPTS,DATA}. It is not a replacement
# for distcheck: check-extra-dist.py's docstring lists what it does not see,
# and docs/refactoring/T3.45.md section 3 explains why.
#
# Exit codes follow the automake simple-test protocol:
#   0  -> PASS (every dist path resolves)
#   77 -> SKIP (no usable python3)
#   1  -> FAIL (at least one dist path names no file)
#
# PYTHON and abs_top_srcdir are exported by AM_TESTS_ENVIRONMENT in
# tests/Makefile.am; fall back to sane defaults when run by hand.

: "${PYTHON:=python3}"

# AM_PATH_PYTHON sets PYTHON=: when no interpreter was found.
if test "x$PYTHON" = "x:"; then
    echo "SKIP: no python3 interpreter detected at configure time"
    exit 77
fi

if test -z "$abs_top_srcdir"; then
    abs_top_srcdir=$(cd "$(dirname "$0")/.." && pwd)
    export abs_top_srcdir
fi

exec "$PYTHON" "$abs_top_srcdir/tests/check-extra-dist.py"
