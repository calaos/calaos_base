#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#install dependencies

sudo apt-get install libsigc++-2.0-dev libjansson-dev libcurl4-gnutls-dev luajit libluajit-5.1-dev libow-dev libusb-dev libola-dev curl imagemagick libgtest-dev python-autobahn

mkdir $HOME/local

pushd $HOME

wget_retry https://dist.libuv.org/dist/v1.14.1/libuv-v1.14.1.tar.gz
tar xzvf libuv-v1.14.1.tar.gz
pushd libuv-v1.14.1
./autogen.sh
./configure --prefix=$HOME/local
make
make install
popd

wget_retry https://github.com/knxd/knxd/archive/v0.14.17.tar.gz
tar xzvf v0.14.17.tar.gz
pushd knxd-0.14.17
./bootstrap.sh
./configure --prefix=$HOME/local
make
make install
popd

popd
