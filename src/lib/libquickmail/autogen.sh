#!/bin/sh

# fix error: Libtool library used but `LIBTOOL' is undefined
#echo /mingw32/share/aclocal > /share/aclocal/dirlist

# fix error: required file `./ltmain.sh' not found
#autoreconf -i

# create necessary macros in aclocal.m4 for automake
aclocal --force
# create ltmain.sh
libtoolize --force
# create configure
autoconf --force
# create config.h.in
autoheader --force
# create Makefile.in from Makefile.am
automake --force-missing --add-missing
