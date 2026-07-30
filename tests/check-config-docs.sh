#!/bin/sh
#
#  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
#
#  This file is part of Calaos.
#
#  Calaos is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the License, or
#  (at your option) any later version.
#
#  Calaos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with Calaos. If not, see <http://www.gnu.org/licenses/>.
#
# ----------------------------------------------------------------------------
# Anti-drift guard for the generated option documentation.
#
# docs/16_config_options.md is produced by "calaos_config options --markdown".
# A generated file that is committed and never re-checked catches exactly the
# disease this whole registry was built to cure -- docs/11_config_persistence.md
# used to document ntp_server and mail_to, two keys that existed in no source
# file at all.
#
# This test regenerates the document and diffs it against the committed one.
#
# It is a separate script from check-config-options.sh on purpose: the two
# guards fail for unrelated reasons and are fixed in unrelated ways (edit the
# registry vs. re-run one command), and keeping them apart means "make check"
# names the one that broke instead of a single opaque config test.
# ----------------------------------------------------------------------------

set -u

DOC=docs/16_config_options.md

# ---------------------------------------------------------------------------
# Locate the tree. Under "make check" automake exports abs_top_srcdir and
# abs_top_builddir (see AM_TESTS_ENVIRONMENT in tests/Makefile.am). Run by
# hand, fall back to the location of this script.
# ---------------------------------------------------------------------------
scriptdir=`dirname "$0"`
scriptdir=`cd "$scriptdir" && pwd`

: ${abs_top_srcdir:="$scriptdir/.."}
abs_top_srcdir=`cd "$abs_top_srcdir" && pwd`

if [ -n "${abs_top_builddir-}" ]; then
    abs_top_builddir=`cd "$abs_top_builddir" && pwd`
elif [ -x "$abs_top_srcdir/_build/src/bin/tools/calaos_config" ]; then
    abs_top_builddir="$abs_top_srcdir/_build"
else
    abs_top_builddir="$abs_top_srcdir"
fi

CALAOS_CONFIG="$abs_top_builddir/src/bin/tools/calaos_config"
COMMITTED="$abs_top_srcdir/$DOC"

if [ ! -x "$CALAOS_CONFIG" ]; then
    echo "FAIL: calaos_config not found at $CALAOS_CONFIG" >&2
    echo "      Build the tree first (cd _build && make)." >&2
    exit 1
fi
if [ ! -f "$COMMITTED" ]; then
    echo "FAIL: $COMMITTED is missing." >&2
    echo "      Generate it with: $CALAOS_CONFIG options --markdown > $DOC" >&2
    exit 1
fi

tmpdir=`mktemp -d 2>/dev/null` || tmpdir=/tmp/check-config-docs.$$
mkdir -p "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

# --config/--cache point at a throwaway directory: this test must never read,
# nor create, the real /etc/calaos/local_config.xml. The generated document
# only depends on the registry, not on the values of that file.
if ! "$CALAOS_CONFIG" --config "$tmpdir" --cache "$tmpdir" --color=never \
        options --markdown >"$tmpdir/generated.md" 2>"$tmpdir/stderr"; then
    echo "FAIL: '$CALAOS_CONFIG options --markdown' failed:" >&2
    cat "$tmpdir/stderr" >&2
    exit 1
fi

if [ ! -s "$tmpdir/generated.md" ]; then
    echo "FAIL: 'calaos_config options --markdown' produced nothing." >&2
    exit 1
fi

if diff -u "$COMMITTED" "$tmpdir/generated.md" >"$tmpdir/diff"; then
    echo "check-config-docs: $DOC matches the generator"
    exit 0
fi

echo "FAIL: $DOC no longer matches 'calaos_config options --markdown'." >&2
echo "      The registry in src/lib/ConfigOptions.cpp changed and the generated" >&2
echo "      document was not refreshed. Regenerate it from the top of the tree:" >&2
echo "" >&2
echo "          _build/src/bin/tools/calaos_config options --markdown > $DOC" >&2
echo "" >&2
echo "      Difference (-committed / +generated):" >&2
sed 's/^/      /' "$tmpdir/diff" >&2
exit 1
