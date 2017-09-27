#!/bin/bash

set -e

export PROJECT_NAME=calaos_base
export TRAVIS_BUILD_DIR=$(pwd)
export BUILD_TAG=$(git tag --points-at=HEAD --sort version:refname | head -n 1)

export PATH=$HOME/local/bin:$PATH
export PKG_CONFIG_PATH=$HOME/local/lib/pkgconfig

#Usage: get_version /path/to/repo
function get_version()
{
    repo=$1
    pushd $repo > /dev/null
    git describe --tags --abbrev=0
    popd > /dev/null
}

#Usage: make_version /path/to/repo
function make_version()
{
    echo "#ifndef VERSION__H" > $1/src/version.h
    echo "#define VERSION__H" >> $1/src/version.h
    echo "#define APP_VERSION \"$(get_version $1)\"" >> $1/src/version.h
    echo "#endif" >> $1/src/version.h
}

function wget_retry()
{
    count=0
    while [ $count -le 4 ]; do
        echo Downloading $@
        set +e
        wget --retry-connrefused --waitretry=1 --read-timeout=20 --timeout=15 -t 0 --continue $@
        ret=$?
        set -e
        if [ $ret = 0 ]
        then
            return 0
        fi
        sleep 1
        count=$((count+1))
        echo Download failed.
    done
    return 1
}

