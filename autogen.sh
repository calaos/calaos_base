#!/bin/sh

cd $(dirname "$0")

rm -rf autom4te.cache
rm -f aclocal.m4 ltmain.sh

set -e

libtoolize
autoreconf -vif

if [ -z "$NOCONFIGURE" ]; then
	./configure "$@"
fi
