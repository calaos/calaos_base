#!/bin/sh

cd ..
find ./src -type f -name "*.cpp" -o -name "*.h" -o -name "*.c" | sort > po/POTFILES.in
./autogen.sh
cd po
make update-po
