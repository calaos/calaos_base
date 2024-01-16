
CALAOS_VERSION
AC_INIT([calaos],[calaos_version],[team@calaos.fr])
AC_PREREQ([2.71])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([tar-ustar 1.6 dist-xz subdir-objects foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

LT_PREREQ([2.2])
LT_INIT([win32-dll])

AM_GNU_GETTEXT_VERSION([0.19])

m4_ifdef([AM_GNU_GETTEXT], [
AM_GNU_GETTEXT([external])
po_makefile_in=po/Makefile.in
have_po="yes"
],[
have_po="no"
])
AC_SUBST(LTLIBINTL)

if test "x${POSUB}" = "x" ; then
   have_po="no"
fi

AM_CONDITIONAL([HAVE_PO], [test "x${have_po}" = "xyes"])

### Needed information

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

### Checks for programs
AC_PROG_CXX
AC_PROG_CC
AM_PROG_CC_C_O
AX_CXX_COMPILE_STDCXX_14([noext],[mandatory])

# pkg-config
PKG_PROG_PKG_CONFIG

### Checks for libraries

requirements_calaos_common="libuv >= 1.19.0 sigc++-2.0 >= 2.4.1 jansson >= 2.5 libcurl >= 7.20.0 luajit sqlite3"
PKG_CHECK_MODULES([CALAOS_COMMON], [${requirements_calaos_common}])
AC_SUBST([requirements_calaos_common])

requirements_calaos_server="libuv >= 1.19.0 libcurl"
PKG_CHECK_MODULES([CALAOS_SERVER], [${requirements_calaos_server}])
AC_SUBST([requirements_calaos_server])

CALAOS_COMMON_LIBS="${CALAOS_COMMON_LIBS} ${LUA_LIBS} ${lua_libs}"
CALAOS_COMMON_CFLAGS="${CALAOS_COMMON_CFLAGS} ${LUA_CFLAGS}"

### Checks for compiler characteristics
AC_C_BIGENDIAN
AC_C_INLINE

### Checks for linker characteristics
lt_enable_auto_import=""
case "${host_os}" in
   mingw*)
      lt_enable_auto_import="-Wl,--enable-auto-import"
   ;;
esac
AC_SUBST([lt_enable_auto_import])

EFL_CHECK_COMPILER_FLAGS([CALAOS_COMMON], [-Wall -Wextra -Wno-unused-parameter -Wshadow -Wpointer-arith -Wno-missing-field-initializers -fvisibility=hidden -fdata-sections -ffunction-sections])

### Checks for header files
AC_HEADER_ASSERT
AC_CHECK_HEADERS([unistd.h])

have_owcapi="no"

AC_CHECK_HEADERS([owcapi.h], [have_owcapi="yes"])
AM_CONDITIONAL([HAVE_OWCAPI], [test "x${have_owcapi}" = "xyes"])
if test "x${have_owcapi}" == "xyes"
then
    AC_CHECK_LIB([owcapi], [OW_init],, AC_MSG_ERROR(Missing owfs library))
fi

have_libusb="no"
PKG_CHECK_MODULES([LIBUSB], [libusb-1.0], [have_libusb="yes"], [have_libusb="no"])
AM_CONDITIONAL([HAVE_LIBUSB], [test "x${have_libusb}" = "xyes"])
if test "x${have_libusb}" == "xyes"
then
     AC_DEFINE([HAVE_LIBUSB], [1], [libusb])
fi

have_libola="no"
PKG_CHECK_MODULES([LIBOLA], [libola], [have_libola="yes"], [have_libola="no"])
AM_CONDITIONAL([HAVE_LIBOLA], [test "x${have_libola}" = "xyes"])
if test "x${have_libola}" == "xyes"
then
     AC_DEFINE([HAVE_LIBOLA], [1], [libola])
fi

have_libknx="no"
AC_CHECK_HEADERS([eibclient.h], [have_libknx="yes"])
AM_CONDITIONAL([HAVE_LIBKNX], [test "x${have_libknx}" = "xyes"])
if test "x${have_libknx}" == "xyes"
then
     AC_CHECK_LIB([eibclient], [EIBSocketLocal],, AC_MSG_ERROR(Missing eibclient library))
     AC_DEFINE([HAVE_LIBKNX], [1], [libknx])
fi

AC_CHECK_PROG(HAVE_CURLBIN, [curl], ["yes"])
if test "x${HAVE_CURLBIN}" == "xyes"
then
    CURLBIN_INFO="yes"
else
    CURLBIN_INFO="No, install curl binary before running calaos_server"
fi

AC_CHECK_PROG(HAVE_CONVERT, [convert], ["yes"])
if test "x${HAVE_CONVERT}" == "xyes"
then
    CONVERT_INFO="yes"
else
    CONVERT_INFO="No, install imagemagick convert tool"
fi

have_mosquitto="no"
AC_CHECK_HEADERS([mosquitto.h], [have_mosquitto="yes"])
AM_CONDITIONAL([HAVE_LIBMOSQUITTO], [test "x${have_mosquitto}" = "xyes"])
if test "x${have_mosquitto}" == "xyes"
then
    AC_CHECK_LIB([mosquitto], [mosquitto_lib_init],, AC_MSG_ERROR(Missing mosquitto library))
    AC_DEFINE([HAVE_LIBMOSQUITTO], [1], [libmosquitto])
fi

# Configure pthreads.
ACX_PTHREAD([have_pthread=yes])

#check for gtest
AC_LANG_PUSH([C++])
AC_CHECK_HEADERS([gtest/gtest.h], [have_gtest="yes"])
AM_CONDITIONAL([HAVE_GTEST], [test "x${have_gtest}" = "xyes"])
AC_LANG_POP([C++])
if test "x${have_gtest}" == "xyes"
then
     GTEST_INFO="make check"
else
     GTEST_INFO="install gtest to run this test"
fi

#check for autobahn test suite
AC_CHECK_PROG(HAVE_AUTOBAHN, wstest, yes)
AM_CONDITIONAL([HAVE_AUTOBAHN], [test "x${HAVE_AUTOBAHN}" = "xyes"])
if test "x${HAVE_AUTOBAHN}" == "xyes"
then
     WS_INFO="make check"
else
     WS_INFO="install autobahn testsuite to run this test"
fi

AC_CONFIG_FILES([
Makefile
tests/Makefile
src/Makefile
src/lib/Makefile
src/bin/Makefile
src/bin/calaos_server/Makefile
src/bin/tools/Makefile
data/Makefile
data/debug/Makefile
data/debug/dist/Makefile
$po_makefile_in
])

AC_OUTPUT

#####################################################################
## Info

function colorOut()
{
    if test "x${1}" == "xyes"
    then
        echo -n $(tput setaf 2)${1}$(tput sgr0)
    else
        echo -n $(tput setaf 1)$@$(tput sgr0)
    fi
}

echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo
echo "Compilation............................: make (or gmake)"
echo "  CPPFLAGS.............................: $CPPFLAGS"
echo "  CFLAGS...............................: $CFLAGS"
echo "  CXXFLAGS.............................: $CXXFLAGS"
echo "  LDFLAGS..............................: $LDFLAGS"
echo
echo "Test Suite:"
echo "  gtest library........................: $GTEST_INFO"
echo "  Autobahn Websocket test suite........: $WS_INFO"
echo
echo "Installation...........................: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............................: $prefix"
echo
echo "Dependencies:"
echo "  UrlDownloader curl support...........: $(colorOut $CURLBIN_INFO)"
echo "  UrlDownloader picture resizing.......: $(colorOut $CONVERT_INFO)"
echo
echo "Hardware support:"
echo "  One Wire support.....................: $(colorOut $have_owcapi)"
echo "  Libusb support.......................: $(colorOut $have_libusb)"
echo "  Open Lightning Architecture support..: $(colorOut $have_libola)"
echo "  Eib/KNX support (eibd or knxd).......: $(colorOut $have_libknx)"
echo "  MQTT support (libmosquittopp)........: $(colorOut $have_mosquitto)"
echo
