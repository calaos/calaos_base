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
# Anti-drift guard for the local_config.xml option registry.
#
# src/lib/ConfigOptions.cpp is meant to be the single source of truth for every
# key of local_config.xml, but nothing in the compiler enforces it: a driver can
# call Utils::get_config_option("new_thing") and never declare it. That is
# exactly how docs/11_config_persistence.md came to document two keys
# (ntp_server, mail_to) that existed in no source file at all.
#
# This test makes that failure loud at "make check" time. Three checks:
#
#   1. every config key referenced by the sources exists in the registry;
#   2. every registry entry that no source references declares a consumer other
#      than Server -- that is what tells "belongs to Calaos Home or to the
#      Python MCP sidecar" apart from "orphaned by accident";
#   3. no key of ConfigOptions::obsoleteKeys() comes back.
#
# Checks 1 and 2 are deliberately one-way: a key used by the code MUST be in
# the registry, but a key of the registry MAY be absent from the code -- seven
# options belong to Calaos Home and three to the Python MCP sidecar, and
# calaos_base never mentions them.
#
# The registry side is read from "calaos_config options --json" rather than
# parsed out of the C++: the binary is built by the time the tests run, and
# that keeps this script honest about what the registry really exposes.
#
# The source side is scraped with the explicit, documented rules of the
# "SCAN RULES" section below. They are deliberately dumb and narrow: a false
# positive is a loud, fixable failure, whereas a false negative silently lets a
# key through, which is the very thing this test exists to prevent.
# ----------------------------------------------------------------------------

# Not "set -e": every check reports all of its failures before exiting.
set -u

me=tests/check-config-options.sh

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
SRCDIR="$abs_top_srcdir/src"

if [ ! -x "$CALAOS_CONFIG" ]; then
    echo "FAIL: calaos_config not found at $CALAOS_CONFIG" >&2
    echo "      Build the tree first (cd _build && make)." >&2
    exit 1
fi
if [ ! -d "$SRCDIR" ]; then
    echo "FAIL: source tree not found at $SRCDIR" >&2
    exit 1
fi

tmpdir=`mktemp -d 2>/dev/null` || tmpdir=/tmp/check-config-options.$$
mkdir -p "$tmpdir" || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

: >"$tmpdir/failures"

# Failures are recorded in a file, not in a variable: part of the scan runs in
# a subshell (pipeline), where a shell variable would be lost.
fail()
{
    echo "FAIL: $*" >&2
    echo x >>"$tmpdir/failures"
}

# calaos_config initialises its config path before running any action, so point
# it at a throwaway directory: this test must never read, nor create, the real
# /etc/calaos/local_config.xml.
run_config()
{
    "$CALAOS_CONFIG" --config "$tmpdir" --cache "$tmpdir" --color=never "$@" \
        2>"$tmpdir/stderr"
}

# ---------------------------------------------------------------------------
# The registry side
# ---------------------------------------------------------------------------
if ! run_config options --json >"$tmpdir/registry.json"; then
    echo "FAIL: '$CALAOS_CONFIG options --json' failed:" >&2
    cat "$tmpdir/stderr" >&2
    exit 1
fi

# One "key" field per option, always emitted as: <spaces>"key": "<value>",
json_keys()
{
    sed -n 's/^[[:space:]]*"key":[[:space:]]*"\([^"]*\)".*$/\1/p' "$1"
}

json_keys "$tmpdir/registry.json" | sort -u >"$tmpdir/registry.keys"

# Options the server itself is declared to consume. Anything in there that the
# sources never mention is an orphan (check 2).
if ! run_config options --json --consumer=server >"$tmpdir/server.json"; then
    echo "FAIL: '$CALAOS_CONFIG options --json --consumer=server' failed:" >&2
    cat "$tmpdir/stderr" >&2
    exit 1
fi
json_keys "$tmpdir/server.json" | sort -u >"$tmpdir/server.keys"

# The "obsolete_keys" array of the same document.
awk '
    /"obsolete_keys"[ \t]*:/ { inside = 1; next }
    inside && /\]/           { inside = 0 }
    inside {
        gsub(/[",]/, "")
        gsub(/^[ \t]+|[ \t]+$/, "")
        if ($0 != "") print
    }
' "$tmpdir/registry.json" | sort -u >"$tmpdir/obsolete.keys"

nb_registry=`wc -l <"$tmpdir/registry.keys" | tr -d ' '`
nb_obsolete=`wc -l <"$tmpdir/obsolete.keys" | tr -d ' '`

[ "$nb_registry" -eq 0 ] && \
    fail "the registry is empty: 'calaos_config options --json' returned no key"
[ "$nb_obsolete" -eq 0 ] && \
    fail "ConfigOptions::obsoleteKeys() is empty: the obsolete-key guard is a no-op"

# ---------------------------------------------------------------------------
# SCAN RULES -- how a config key is recognised in the sources.
#
# Every rule appends "<key><TAB><file>:<line>" to $tmpdir/used.keys.
#
# Rule A  any source  Utils::{get,set,del}_config_option("literal", ...).
#                     The plural {get,set}_config_options() take a Params and no
#                     key; the regex requires a '(' right after the singular
#                     name, so it never matches them.
# Rule B  any source  the same calls with the key passed as a constant, e.g.
#                     get_config_option(MCP_TOKEN_KEY, true). The identifier is
#                     resolved against the `const char *NAME = "..."` /
#                     `const std::string NAME = "..."` definitions of the same
#                     file. An identifier that resolves to nothing is a hard
#                     failure unless it is in DYNAMIC KEY EXCEPTIONS below.
# Rule C  src/lib/Utils.cpp
#                     Utils::initConfigOptions() seeds the defaults through a
#                     `{ "key", "value" }` initialiser list and completes an
#                     existing file through Params::Exists("key") /
#                     Params::Add("key", ...). Those three patterns appear
#                     nowhere else in Utils.cpp, so the whole file is scanned.
# Rule D  src/lib/TimeRange.cpp
#                     TimeRange reads latitude/longitude out of the options map
#                     with Params::Exists("key"). Only Exists() is scanned here:
#                     the Params::Add() calls of this file serialise a time
#                     range (day, start_hour, ...), not config options.
# Rule E  src/bin/calaos_mcp/python/calaos_mcp/config.py
#                     The Python sidecar parses local_config.xml on its own and
#                     picks its keys with options.get("key") / _opt_int("key").
#
# Not scanned on purpose: the RemoteUI "device_type" IO parameter and its
# friends. Those are IO params that happen to share a name with a former config
# key; they have nothing to do with local_config.xml.
# ---------------------------------------------------------------------------

: >"$tmpdir/used.keys"

# --- DYNAMIC KEY EXCEPTIONS ------------------------------------------------
# First-argument expressions that are legitimately not a config key. Matched on
# the exact text, whitespace squeezed. Every entry must say why it is here.
#
#   "string key", "string _key"
#         the definitions and declarations of Utils::{get,set,del}_config_option
#         themselves (src/lib/Utils.cpp, src/lib/Utils.h) -- not call sites.
#   "_key"
#         src/lib/Utils.cpp, those same functions forwarding their own parameter
#         to the unlocked doGetConfigOption() helper.
#   "key"
#         src/bin/tools/calaos_config.cpp `set` action: the key comes from argv.
#         The CLI validates it against the registry at run time instead.
#   "args.params[0]"
#         same file, `get` and `del` actions, key straight out of argv.
# ---------------------------------------------------------------------------
is_dynamic_key_exception()
{
    case "$1" in
        "string key"|"string _key"|"_key"|"key"|"args.params[0]") return 0 ;;
        *) return 1 ;;
    esac
}

# Rules A and B
grep -rnoE '\b(get|set|del)_config_option[[:space:]]*\([^,)]*\)?' "$SRCDIR" \
     --include='*.cpp' --include='*.h' --include='*.c' 2>/dev/null |
sed 's/^\([^:]*\):\([0-9][0-9]*\):/\1|\2|/' |
while IFS='|' read -r file line match; do
    arg=`echo "$match" | sed 's/^.*_config_option[[:space:]]*(//'`

    # "get_config_option()" with no argument at all is prose in a comment, not
    # a call: such a call would not even compile.
    case "$arg" in
        ")"*) continue ;;
    esac

    arg=`echo "$arg" | sed 's/)[[:space:]]*$//; s/^[[:space:]]*//; s/[[:space:]]*$//; s/[[:space:]][[:space:]]*/ /g'`

    case "$arg" in
    '"'*'"')
        # Rule A: string literal
        key=`echo "$arg" | sed 's/^"//; s/"$//'`
        printf '%s\t%s:%s\n' "$key" "$file" "$line" >>"$tmpdir/used.keys"
        ;;
    "")
        fail "$file:$line: cannot read the key of this *_config_option() call" \
             "(argument split over several lines?). Keep the key on the same line" \
             "as the call, or extend the scan rules of $me."
        ;;
    *)
        if is_dynamic_key_exception "$arg"; then
            continue
        fi
        # Rule B: resolve the identifier against a constant of the same file
        ident=`echo "$arg" | sed 's/[^A-Za-z0-9_].*$//'`
        val=`grep -hoE "(char[[:space:]]*\\*|std::string|string)[[:space:]]*$ident[[:space:]]*=[[:space:]]*\"[^\"]*\"" "$file" |
             sed 's/.*"\(.*\)"$/\1/' | head -n 1`
        if [ -n "$val" ]; then
            printf '%s\t%s:%s\n' "$val" "$file" "$line" >>"$tmpdir/used.keys"
        else
            fail "$file:$line: *_config_option() is called with '$arg', which is" \
                 "neither a string literal nor a resolvable 'const char *$ident =" \
                 "\"...\"' of the same file. Either declare the key as such a" \
                 "constant, or add the expression to the DYNAMIC KEY EXCEPTIONS" \
                 "list of $me."
        fi
        ;;
    esac
done

# Rule C -- src/lib/Utils.cpp
utils_cpp="$SRCDIR/lib/Utils.cpp"
if [ -f "$utils_cpp" ]; then
    grep -noE '\{[[:space:]]*"[^"]*"[[:space:]]*,[[:space:]]*"[^"]*"[[:space:]]*\}|\.(Exists|Add)[[:space:]]*\([[:space:]]*"[^"]*"' \
         "$utils_cpp" |
    sed 's|^\([0-9]*\):[^"]*"\([^"]*\)".*$|\2\t'"$utils_cpp"':\1|' >>"$tmpdir/used.keys"
else
    fail "$utils_cpp not found: scan rule C is dead"
fi

# Rule D -- src/lib/TimeRange.cpp
timerange_cpp="$SRCDIR/lib/TimeRange.cpp"
if [ -f "$timerange_cpp" ]; then
    grep -noE '\.Exists[[:space:]]*\([[:space:]]*"[^"]*"' "$timerange_cpp" |
    sed 's|^\([0-9]*\):[^"]*"\([^"]*\)".*$|\2\t'"$timerange_cpp"':\1|' >>"$tmpdir/used.keys"
else
    fail "$timerange_cpp not found: scan rule D is dead"
fi

# Rule E -- the Python MCP sidecar
mcp_config_py="$SRCDIR/bin/calaos_mcp/python/calaos_mcp/config.py"
if [ -f "$mcp_config_py" ]; then
    grep -noE '(options\.get|_opt_int)[[:space:]]*\([[:space:]]*"[^"]*"' "$mcp_config_py" |
    sed 's|^\([0-9]*\):[^"]*"\([^"]*\)".*$|\2\t'"$mcp_config_py"':\1|' >>"$tmpdir/used.keys"
else
    fail "$mcp_config_py not found: scan rule E is dead"
fi

cut -f1 "$tmpdir/used.keys" | sort -u >"$tmpdir/used.uniq"
nb_used=`wc -l <"$tmpdir/used.uniq" | tr -d ' '`

[ "$nb_used" -eq 0 ] && \
    fail "no config key at all was found under $SRCDIR: the scan rules are" \
         "broken and this test would pass whatever the code does"

# Exact, literal lookups: option keys contain '/' and could in theory contain a
# regular-expression metacharacter, so never let one reach a regex engine.
key_is_used()
{
    awk -F'\t' -v k="$1" '$1 == k { found = 1 } END { exit !found }' "$tmpdir/used.keys"
}

where_used()
{
    awk -F'\t' -v k="$1" '$1 == k { print $2 }' "$tmpdir/used.keys" | sort -u | tr '\n' ' '
}

# ---------------------------------------------------------------------------
# Check 3 first, so that a reintroduced dead key gets its own message instead
# of the generic "not in the registry" one.
# ---------------------------------------------------------------------------
for key in `cat "$tmpdir/obsolete.keys"`; do
    if key_is_used "$key"; then
        fail "obsolete config key \"$key\" is used again at: `where_used "$key"`" \
             "-- it is listed in ConfigOptions::obsoleteKeys() and was removed from" \
             "the code on purpose (commit 8684877b, \"Remove five dead config keys" \
             "and the unused NTPClock class\"). Either stop using it, or take it" \
             "out of obsoleteKeys() and declare it in the registry."
    fi
done

# ---------------------------------------------------------------------------
# Check 1: every key used by the sources must be declared in the registry.
# ---------------------------------------------------------------------------
for key in `cat "$tmpdir/used.uniq"`; do
    grep -qxF "$key" "$tmpdir/registry.keys" && continue
    grep -qxF "$key" "$tmpdir/obsolete.keys" && continue   # reported by check 3
    fail "config key \"$key\" is used at: `where_used "$key"`" \
         "but is not declared in src/lib/ConfigOptions.cpp." \
         "Add a ConfigOption(\"$key\", category, type) entry to the registry" \
         "(label, doc, default, consumer), then regenerate the documentation:" \
         "calaos_config options --markdown > docs/16_config_options.md"
done

# ---------------------------------------------------------------------------
# Check 2: a registry entry no source references must belong to somebody else
# than the server -- Calaos Home or the Python MCP sidecar.
# ---------------------------------------------------------------------------
for key in `cat "$tmpdir/registry.keys"`; do
    grep -qxF "$key" "$tmpdir/used.uniq" && continue
    if grep -qxF "$key" "$tmpdir/server.keys"; then
        fail "config option \"$key\" is declared with consumer(Server) in" \
             "src/lib/ConfigOptions.cpp, but no source under $SRCDIR ever reads it." \
             "Either the code that used it is gone -- drop the entry, or move the" \
             "key to ConfigOptions::obsoleteKeys() -- or it really belongs to" \
             "Calaos Home / the MCP sidecar, in which case fix its .consumer() mask."
    fi
done

# ---------------------------------------------------------------------------
nb_fail=`wc -l <"$tmpdir/failures" | tr -d ' '`

echo "check-config-options: $nb_used keys used by the sources," \
     "$nb_registry in the registry, $nb_obsolete obsolete"

if [ "$nb_fail" -ne 0 ]; then
    echo "check-config-options: $nb_fail problem(s) found" >&2
    exit 1
fi

echo "check-config-options: OK"
exit 0
