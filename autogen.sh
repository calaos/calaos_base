#!/bin/sh

cd $(dirname "$0")

rm -rf autom4te.cache
rm -f aclocal.m4 ltmain.sh

set -e

#On osx:
# brew info libtool says:
# In order to prevent conflicts with Apple's own libtool we have prepended a "g"
# so, you have instead: glibtool and glibtoolize.
if glibtoolize --version > /dev/null 2>&1; then
    glibtoolize
else
    libtoolize
fi

autoreconf -vif

if [ -z "$NOCONFIGURE" ]; then
	./configure "$@"
fi
