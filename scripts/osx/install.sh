#!/bin/bash

set -ev

SCRIPTDIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
source $SCRIPTDIR/../lib.sh

#install dependencies
brew update
brew install libsigc++ jansson curl luajit owfs libusb ola
sudo apt-get install libsigc++-2.0-dev libjansson-dev libcurl4-gnutls-dev luajit libluajit-5.1-dev libow-dev libusb-dev libola-dev curl imagemagick libgtest-dev python-autobahn

mkdir $HOME/local

pushd $HOME

wget_retry https://github.com/google/googletest/archive/release-1.8.0.zip
unzip release-1.8.0.zip
pushd googletest-release-1.8.0
mkdir build && cd build
cmake ..
make
cp -a ../include/gtest /usr/local/include
cp -a *.a /usr/local/lib
popd

popd
