#!/bin/bash
set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

pushd $TRAVIS_BUILD_DIR

export CXX=${COMPILER}
echo "Using compiler: $CXX"
$CXX --version

mkdir -p $HOME/local
./autogen.sh --prefix=$HOME/local CPPFLAGS=-I$LOCAL_DEPS/include LDFLAGS=-L$LOCAL_DEPS/lib --disable-nls
make
make install

#make check
#cat tests/test-suite.log

popd
