#!/bin/sh

rm -rf autom4te.cache
rm -f aclocal.m4 ltmain.sh

autoreconf -vif

if [ -z "$NOCONFIGURE" ]; then
	./configure "$@"
fi
