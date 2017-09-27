#!/bin/bash
set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

pushd $TRAVIS_BUILD_DIR

export CXX=${COMPILER}
echo "Using compiler: $CXX"
$CXX --version

mkdir $HOME/local
./autogen.sh --prefix=$HOME/local
make
make install
make check

echo "[ Installed Files ]"
find $HOME/local

popd
