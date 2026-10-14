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
# calaos_server --gentemplates <dir> writes the device templates the installer
# imports (scripts/create_iodoc runs it next to --gendoc), and exits non-zero
# when it cannot write them. Their content is checked by core/DeviceTemplates_test.

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

tmpdir=`mktemp -d 2>/dev/null` || tmpdir=/tmp/check-gentemplates.$$
mkdir -p "$tmpdir/conf" "$tmpdir/cache" || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

#Bounded: a server that ignores the option starts for good.
run()
{
    timeout 60 "$server" --config "$tmpdir/conf" --cache "$tmpdir/cache" --gentemplates "$1" >"$tmpdir/log" 2>&1
}

if ! "$server" --help 2>&1 | grep -q -- "--gentemplates <path>"; then
    echo "FAIL: --help does not list --gentemplates" >&2
    exit 1
fi

if ! run "$tmpdir/out/sub"; then
    echo "FAIL: --gentemplates exited with an error:" >&2
    cat "$tmpdir/log" >&2
    exit 1
fi

if ! "$PYTHON" - "$tmpdir/out/sub/device_templates.json" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f:
    doc = json.load(f)
ids = [t["id"] for t in doc["templates"]]
expected = ["esphome_climate", "esphome_climate_ir", "thermostat_heat",
            "thermostat_heat_cool", "thermostat_floor_heat_cool"]
if doc["format"] != 1 or ids != expected:
    sys.exit("unexpected document: format %r, templates %r" % (doc["format"], ids))
PYEOF
then
    echo "FAIL: --gentemplates did not write the five templates" >&2
    exit 1
fi

: >"$tmpdir/a_file"
if run "$tmpdir/a_file/sub"; then
    echo "FAIL: --gentemplates succeeded on a path it cannot write to" >&2
    exit 1
fi

echo "check-gentemplates: calaos_server --gentemplates writes the device templates"
