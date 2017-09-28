#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#install dependencies
brew update
brew install libsigc++ jansson curl luajit owfs libusb ola

mkdir $HOME/local

pushd $HOME

wget_retry https://github.com/google/googletest/archive/release-1.8.0.zip
unzip release-1.8.0.zip
pushd googletest-release-1.8.0
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON -DCMAKE_SKIP_RPATH=ON
make
sudo make install
popd

popd
