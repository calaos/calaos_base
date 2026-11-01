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
# calaos_picture is run for each get_camera_pic and get_cover: it has to fetch
# the picture once, keep its URL out of the journal, and tell the server with
# its exit status whether the picture it wrote was resized. curl and convert are
# fakes, and PATH holds nothing else of use: ImageMagick is in the build image,
# and the "no convert" case must not find it.

set -u

top_srcdir="${abs_top_srcdir:-$(cd "$(dirname "$0")/.." && pwd)}"
tool="$top_srcdir/src/bin/tools/calaos_picture"

if [ ! -x "$tool" ]; then
    echo "SKIP: $tool not found or not executable"
    exit 77
fi

bash=$(command -v bash) || { echo "SKIP: no bash"; exit 77; }

work=$(mktemp -d "${TMPDIR:-/tmp}/check-calaos-picture.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT

mkdir "$work/bin"
for needed in getopt mv rm sleep; do
    target=$(command -v "$needed") || { echo "SKIP: no $needed"; exit 77; }
    ln -s "$target" "$work/bin/$needed"
done
realTimeout=$(command -v timeout) || { echo "SKIP: no timeout"; exit 77; }

url='http://cam.invalid/snapshot?token=fake-camera-secret'
out="$work/out.jpg"
failed=0

# Writes to the file given after --output, unless told to fail
cat >"$work/bin/curl" <<EOF
#!/bin/sh
echo "\$*" >>"$work/curl.calls"
[ -f "$work/curl.fails" ] && exit 22
while [ \$# -gt 0 ]; do
    [ "\$1" = "--output" ] && { printf downloaded >"\$2"; exit 0; }
    shift
done
exit 2
EOF

# The last argument is the output file. A convert that hangs notes its pid:
# the exec makes it the process the script has to stop.
cat >"$work/bin/convert" <<EOF
#!/bin/sh
echo "\$*" >>"$work/convert.calls"
[ -f "$work/convert.fails" ] && exit 1
[ -f "$work/convert.hangs" ] && { echo \$\$ >"$work/convert.pid"; exec sleep 60; }
for last; do :; done
printf resized >"\$last"
EOF

# The real timeout, which notes how it was called
cat >"$work/bin/timeout" <<EOF
#!/bin/sh
echo "\$*" >>"$work/timeout.calls"
exec "$realTimeout" "\$@"
EOF

chmod 755 "$work/bin/curl" "$work/bin/convert" "$work/bin/timeout"

run()
{
    rm -f "$out" "$work/curl.calls" "$work/convert.calls" "$work/timeout.calls"
    PATH="$work/bin" "$bash" "$tool" "$@" >"$work/stdout" 2>"$work/stderr"
    status=$?
}

expect()
{
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1: expected '$2', got '$3'"
        failed=1
    fi
}

content()
{
    if [ -f "$out" ]; then cat "$out"; else echo missing; fi
}

curlRuns()
{
    if [ -f "$work/curl.calls" ]; then wc -l <"$work/curl.calls" | tr -d ' '; else echo 0; fi
}

# Resized: one download only
run "$url" "$out" -w 236
expect "resized status" 0 "$status"
expect "resized content" resized "$(content)"
expect "resized downloads" 1 "$(curlRuns)"

if ! grep -q -- "-thumbnail 236" "$work/convert.calls"; then
    echo "FAIL: convert was not asked to resize"
    failed=1
fi

# convert never runs without a deadline
if ! grep -q -- " 30 convert " "$work/timeout.calls"; then
    echo "FAIL: convert is not run under timeout"
    failed=1
fi

# Nothing to do: the picture goes through as it came
run "$url" "$out"
expect "plain status" 0 "$status"
expect "plain content" downloaded "$(content)"
expect "plain downloads" 1 "$(curlRuns)"

# Nothing of the arguments in the output of the script
run "$url" "$out" -w 236
if grep -q -e fake-camera-secret -e cam.invalid "$work/stdout" "$work/stderr"; then
    echo "FAIL: the URL leaks in the output of calaos_picture"
    failed=1
fi

# A convert that fails leaves the frame as it came, and says so
touch "$work/convert.fails"
run "$url" "$out" -w 236
expect "convert failed status" 3 "$status"
expect "convert failed content" downloaded "$(content)"
expect "convert failed downloads" 1 "$(curlRuns)"
rm -f "$work/convert.fails"

# No convert at all: same answer
mv "$work/bin/convert" "$work/convert.kept"
run "$url" "$out" -w 236
expect "no convert status" 3 "$status"
expect "no convert content" downloaded "$(content)"
mv "$work/convert.kept" "$work/bin/convert"

# A failed download writes nothing
touch "$work/curl.fails"
run "$url" "$out" -w 236
expect "curl failed status" 1 "$status"
expect "curl failed content" missing "$(content)"
rm -f "$work/curl.fails"

# The server sends SIGTERM when its client leaves, and counts the script as
# running until it exits: convert has to be gone by then, not left behind
touch "$work/convert.hangs"
rm -f "$out" "$work/convert.pid" "$work/script.pid" "$work/script.done"
# The pid is the script's own (exec), and the marker tells when it is reaped:
# kill -0 would still succeed on a zombie
(
    PATH="$work/bin" "$bash" -c 'echo $$ >"$1"; shift; exec "$@"' sh "$work/script.pid" \
        "$bash" "$tool" "$url" "$out" -w 236 >"$work/stdout" 2>"$work/stderr"
    touch "$work/script.done"
) &

# Polls for at most 5 s: a bound, not a delay
waitFor()
{
    n=0
    while [ "$n" -lt 50 ] && ! eval "$1"; do
        sleep 0.1
        n=$((n + 1))
    done
    eval "$1"
}

if ! waitFor '[ -s "$work/convert.pid" ]'; then
    echo "FAIL: convert did not start"
    failed=1
else
    convertPid=$(cat "$work/convert.pid")
    scriptPid=$(cat "$work/script.pid")
    kill -TERM "$scriptPid"
    if ! waitFor '[ -f "$work/script.done" ]'; then
        echo "FAIL: calaos_picture outlives SIGTERM while convert runs"
        failed=1
    fi
    if ! waitFor '! kill -0 "$convertPid" 2>/dev/null'; then
        echo "FAIL: convert survives calaos_picture"
        failed=1
    fi
    expect "terminated content" missing "$(content)"
    if ls "$out"_* >/dev/null 2>&1; then
        echo "FAIL: work files left behind"
        failed=1
    fi
fi
kill -KILL "${scriptPid:-}" "${convertPid:-}" 2>/dev/null
wait
rm -f "$work/convert.hangs"

# Too few arguments
run "$url"
expect "usage status" 1 "$status"

if grep -n -E '^[[:space:]]*set -[a-z]*x' "$tool"; then
    echo "FAIL: calaos_picture traces its commands, URL included"
    failed=1
fi

exit "$failed"
