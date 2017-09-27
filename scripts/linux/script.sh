#!/bin/bash
set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

pushd $TRAVIS_BUILD_DIR

export CXX=${COMPILER}
echo "Using compiler: $CXX"
$CXX --version

./autogen.sh --prefix=$HOME/local
make
make install
make check

popd
