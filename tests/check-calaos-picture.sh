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
for needed in getopt mv rm; do
    target=$(command -v "$needed") || { echo "SKIP: no $needed"; exit 77; }
    ln -s "$target" "$work/bin/$needed"
done

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

# The last argument is the output file
cat >"$work/bin/convert" <<EOF
#!/bin/sh
echo "\$*" >>"$work/convert.calls"
[ -f "$work/convert.fails" ] && exit 1
for last; do :; done
printf resized >"\$last"
EOF

chmod 755 "$work/bin/curl" "$work/bin/convert"

run()
{
    rm -f "$out" "$work/curl.calls" "$work/convert.calls"
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

# Too few arguments
run "$url"
expect "usage status" 1 "$status"

if grep -n -E '^[[:space:]]*set -[a-z]*x' "$tool"; then
    echo "FAIL: calaos_picture traces its commands, URL included"
    failed=1
fi

exit "$failed"
