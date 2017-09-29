#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#install dependencies

sudo apt-get install autopoint libjansson-dev libcurl4-gnutls-dev luajit libluajit-5.1-dev libow-dev libusb-1.0.0-dev curl imagemagick libev-dev gcc-5 g++-5
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

wget_retry https://github.com/fmtlib/fmt/releases/download/4.0.0/fmt-4.0.0.zip
unzip fmt-4.0.0.zip
mkdir -p fmt-4.0.0/build
pushd fmt-4.0.0/build
cmake .. -DCMAKE_INSTALL_PREFIX=$LOCAL_DEPS
make
make install
popd

wget_retry https://github.com/knxd/knxd/archive/v0.14.17.tar.gz
tar xzvf v0.14.17.tar.gz
pushd knxd-0.14.17

./bootstrap.sh
CXX="g++-5" CC="gcc-5" ./configure --prefix=$LOCAL_DEPS --disable-systemd CPPFLAGS=-I$LOCAL_DEPS/include LDFLAGS=-L$LOCAL_DEPS/lib
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

if [ ! -e "$LOCAL_DEPS/lib/libsigc-2.0.so" ]; then

wget_retry https://download.gnome.org/sources/libsigc++/2.10/libsigc++-2.10.0.tar.xz
tar xJvf libsigc++-2.10.0.tar.xz
pushd libsigc++-2.10.0
./configure --prefix=$LOCAL_DEPS
make
make install
popd

fi

if [ ! -e "$LOCAL_DEPS/include/gtest/gtest.h" ]; then

wget_retry https://github.com/google/googletest/archive/release-1.8.0.zip
unzip release-1.8.0.zip
pushd googletest-release-1.8.0/
mkdir build && pushd build
cmake .. -DCMAKE_INSTALL_PREFIX=$LOCAL_DEPS -DBUILD_SHARED_LIBS=ON -DCMAKE_SKIP_RPATH=ON
make
make install
popd
popd

fi

popd
