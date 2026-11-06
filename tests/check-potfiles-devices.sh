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
# A source of src/bin/calaos_server/IO that marks strings with _() but is not
# in po/POTFILES.in has them left out of the catalogue: the IODoc texts of a
# driver are then never translated. Only the files of that directory are
# checked; a file with its strings elsewhere is not this test's business.

scriptdir=`dirname "$0"`
scriptdir=`cd "$scriptdir" && pwd`
: ${abs_top_srcdir:="$scriptdir/.."}
abs_top_srcdir=`cd "$abs_top_srcdir" && pwd`

potfiles="$abs_top_srcdir/po/POTFILES.in"
missing=""
for f in "$abs_top_srcdir"/src/bin/calaos_server/IO/*.cpp; do
    rel="./src/bin/calaos_server/IO/`basename "$f"`"
    if grep -q '[^a-zA-Z_]_("' "$f" && ! grep -qx "$rel" "$potfiles"; then
        missing="$missing $rel"
    fi
done

if [ -n "$missing" ]; then
    echo "FAIL: translatable sources missing from po/POTFILES.in:" >&2
    for f in $missing; do echo "      $f" >&2; done
    exit 1
fi
echo "check-potfiles-devices: every IO source with _() is in po/POTFILES.in"
