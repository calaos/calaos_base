#!/bin/sh
BASENAME=libquickmail-`head -n 1 ChangeLog|sed -e 's/\s*$//'`
TARBALL=$(pwd)/$BASENAME.tar.xz
BINDIST=$BASENAME-binary.zip
FILELIST='*.c *.h *.am *.in README AUTHORS COPYING License.txt NEWS ChangeLog INSTALL INSTALL.msys man1 configure.ac configure autogen.sh autom4te.cache config.guess config.sub depcomp compile install-sh ltmain.sh *.m4 m4 missing'


# create tarball
echo Creating tarball: $TARBALL
rm -rf /tmp/$BASENAME &> /dev/null
mkdir /tmp/$BASENAME
cp -rf $FILELIST /tmp/$BASENAME/
rm $TARBALL &> /dev/null
pushd /tmp &> /dev/null
tar cfJ $TARBALL $BASENAME --remove-files
popd &> /dev/null

#clean up
rm -rf /tmp/$BASENAME &> /dev/null
