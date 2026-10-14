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
# scripts/create_iodoc is what the CI runs to publish io_doc.json and
# device_templates.json (the installer compares its copies against them):
#  - it must work when HOME cannot be written, as in a CI container or a
#    packaging sandbox;
#  - it writes the three files for en and fr;
#  - the MCP Makefile installs a prebuilt io_doc.json (cross build) and refuses
#    a file that is not one.

set -u

scriptdir=`dirname "$0"`
scriptdir=`cd "$scriptdir" && pwd`
: ${abs_top_srcdir:="$scriptdir/.."}
: ${abs_top_builddir:="$abs_top_srcdir"}
: ${PYTHON:=python3}

server="$abs_top_builddir/src/bin/calaos_server/calaos_server"
if [ ! -x "$server" ]; then
    echo "FAIL: calaos_server not found at $server" >&2
    exit 1
fi

tmpdir=`mktemp -d 2>/dev/null` || tmpdir=/tmp/check-iodoc-generation.$$
mkdir -p "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

#A HOME that is a file: nothing can be created under it.
: >"$tmpdir/not_a_dir"

HOME="$tmpdir/not_a_dir" CALAOS_SERVER="$server" timeout 300 \
    "$abs_top_srcdir/scripts/create_iodoc" "$tmpdir/doc" >"$tmpdir/log" 2>&1
for lang in en fr; do
    for f in io_doc.json io_doc.md device_templates.json; do
        if [ ! -s "$tmpdir/doc/$lang/$f" ]; then
            echo "FAIL: create_iodoc did not write $lang/$f:" >&2
            cat "$tmpdir/log" >&2
            exit 1
        fi
    done
done

if ! "$PYTHON" - "$tmpdir/doc" <<'PYEOF'
import json, sys
for lang in ("en", "fr"):
    with open("%s/%s/io_doc.json" % (sys.argv[1], lang)) as f:
        doc = json.load(f)
    if not doc.get("devices") or "proxy" in doc:
        sys.exit("%s/io_doc.json: devices missing, or a device among the IO types" % lang)
    with open("%s/%s/device_templates.json" % (sys.argv[1], lang)) as f:
        tpl = json.load(f)
    if len(tpl["templates"]) != 5:
        sys.exit("%s/device_templates.json: %d templates" % (lang, len(tpl["templates"])))
PYEOF
then
    echo "FAIL: the generated documents are not the expected ones" >&2
    exit 1
fi

#Prebuilt document (cross build): installed as is, and a wrong file is refused.
mcpdir="$abs_top_builddir/src/bin/calaos_mcp"
if [ -f "$mcpdir/Makefile" ]; then
    mkdir -p "$tmpdir/mk"
    mk="make -s -f $mcpdir/Makefile -C $tmpdir/mk CALAOS_SERVER=$server"
    if ! $mk io_doc.json IODOC_PREBUILT="$tmpdir/doc/fr/io_doc.json" >"$tmpdir/mk.log" 2>&1 ||
       ! cmp -s "$tmpdir/mk/io_doc.json" "$tmpdir/doc/fr/io_doc.json"; then
        echo "FAIL: IODOC_PREBUILT was not installed as io_doc.json:" >&2
        cat "$tmpdir/mk.log" >&2
        exit 1
    fi
    rm -f "$tmpdir/mk/io_doc.json"
    echo '{}' >"$tmpdir/empty.json"
    if $mk io_doc.json IODOC_PREBUILT="$tmpdir/empty.json" >"$tmpdir/mk.log" 2>&1; then
        echo "FAIL: IODOC_PREBUILT accepted a document without devices" >&2
        exit 1
    fi
fi

echo "check-iodoc-generation: create_iodoc and the prebuilt io_doc.json"
