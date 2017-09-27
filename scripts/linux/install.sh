#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#install dependencies

sudo apt-get install autopoint libsigc++-2.0-dev libjansson-dev libcurl4-gnutls-dev luajit libluajit-5.1-dev libow-dev libusb-1.0.0-dev curl imagemagick libev-dev gcc-5 g++-5
#For OLA
sudo apt-get install libcppunit-dev bison flex uuid-dev libprotobuf-dev protobuf-compiler libprotoc-dev

mkdir -p $LOCAL_DEPS

pushd $HOME

#check to see if deps folder is empty
if [ ! -e "$LOCAL_DEPS/lib/libuv.so" ]; then

wget_retry https://dist.libuv.org/dist/v1.14.1/libuv-v1.14.1.tar.gz
tar xzvf libuv-v1.14.1.tar.gz
pushd libuv-v1.14.1
./autogen.sh
./configure --prefix=$LOCAL_DEPS
make
make install
popd

wget_retry https://github.com/knxd/knxd/archive/v0.14.17.tar.gz
tar xzvf v0.14.17.tar.gz
pushd knxd-0.14.17

./bootstrap.sh
CXX="g++-5" CC="gcc-5" ./configure --prefix=$LOCAL_DEPS --disable-systemd
make
make install
popd

wget_retry https://github.com/OpenLightingProject/ola/releases/download/0.10.5/ola-0.10.5.tar.gz
tar xzvf ola-0.10.5.tar.gz
pushd ola-0.10.5
./configure --prefix=$LOCAL_DEPS
make
make install
popd

else
    echo "Using cached deps folder."
fi

wget_retry https://github.com/google/googletest/archive/release-1.8.0.zip
unzip release-1.8.0.zip
pushd googletest-release-1.8.0/
mkdir build && pushd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=ON -DCMAKE_SKIP_RPATH=ON
make
sudo make install
popd
popd

popd
