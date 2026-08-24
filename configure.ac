
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
AX_CXX_COMPILE_STDCXX_20([noext],[mandatory])

# pkg-config
PKG_PROG_PKG_CONFIG

### Checks for libraries

requirements_calaos_common="libuv >= 1.19.0 sigc++-2.0 >= 2.4.1 jansson >= 2.5 libcurl >= 7.20.0 luajit sqlite3 openssl >= 1.1.0"
PKG_CHECK_MODULES([CALAOS_COMMON], [${requirements_calaos_common}])
AC_SUBST([requirements_calaos_common])

requirements_calaos_server="libuv >= 1.19.0 libcurl openssl >= 1.1.0"
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

### pugixml (E4.4): XML DOM with native XPath 1.0. It replaced the vendored
#TinyXML 2.5.3 + TinyXPath, removed from the tree in E4.4e.
#The distro package is the default path (debian: libpugixml-dev, arch: pugixml,
#both ship pugixml.pc with XPath enabled). When it is missing, the vendored copy
#in src/lib/pugixml is compiled into libcalaos_common instead, so the build never
#depends on the host having the package installed.
#This block sits after EFL_CHECK_COMPILER_FLAGS on purpose: the vendored branch
#puts a literal $(top_srcdir) into CALAOS_COMMON_CFLAGS, and that must not reach
#a macro which feeds those flags back to the compiler from the shell.
have_pugixml_system="no"
PKG_CHECK_MODULES([PUGIXML], [pugixml >= 1.10],
                  [have_pugixml_system="yes"],
                  [have_pugixml_system="no"])
if test "x${have_pugixml_system}" = "xyes"
then
    requirements_calaos_common="${requirements_calaos_common} pugixml >= 1.10"
    requirements_calaos_server="${requirements_calaos_server} pugixml >= 1.10"
    PUGIXML_INFO="system $(${PKG_CONFIG} --modversion pugixml) (pkg-config)"
else
    #Single quotes: $(top_srcdir) must survive configure and be expanded by make.
    PUGIXML_CFLAGS='-I$(top_srcdir)/src/lib/pugixml'
    PUGIXML_LIBS=""
    PUGIXML_INFO="vendored src/lib/pugixml (install libpugixml-dev to use the system one)"
fi
AM_CONDITIONAL([HAVE_PUGIXML_SYSTEM], [test "x${have_pugixml_system}" = "xyes"])
AC_SUBST([PUGIXML_CFLAGS])
AC_SUBST([PUGIXML_LIBS])

#Same requirement sets as the other common libs: every consumer of
#@CALAOS_COMMON_*@ / @CALAOS_SERVER_*@ (libcalaos_common, calaos_server, tests)
#gets pugixml for free.
CALAOS_COMMON_CFLAGS="${CALAOS_COMMON_CFLAGS} ${PUGIXML_CFLAGS}"
CALAOS_COMMON_LIBS="${CALAOS_COMMON_LIBS} ${PUGIXML_LIBS}"
CALAOS_SERVER_CFLAGS="${CALAOS_SERVER_CFLAGS} ${PUGIXML_CFLAGS}"
CALAOS_SERVER_LIBS="${CALAOS_SERVER_LIBS} ${PUGIXML_LIBS}"

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

# Check for Python and dependencies
have_python="no"
have_python_roon="no"
have_python_reolink="no"
have_python_mcp="no"

# --without-mcp disables the MCP sidecar entirely.
AC_ARG_WITH([mcp],
    AS_HELP_STRING([--without-mcp], [disable the MCP sidecar (Model Context Protocol)]),
    [with_mcp=$withval],
    [with_mcp=yes])

# Check for Python 3
AM_PATH_PYTHON([3], [
    have_python="yes"

    # Check for pip3
    AC_CHECK_PROG([PYTHON_PIP], [pip3], [yes])
    if test "x${PYTHON_PIP}" = "xyes"; then
        # Check for roonapi
        AC_MSG_CHECKING([for Python roonapi module])
        if $PYTHON -c "import roonapi" 2>/dev/null; then
            have_python_roon="yes"
            AC_MSG_RESULT([yes])
            AC_DEFINE([HAVE_PYTHON_ROON], [1], [Define if python roonapi is available])
        else
            AC_MSG_RESULT([no])
            AC_MSG_WARN([Python roonapi module not found. Install with: pip3 install roonapi])
        fi

        # Check for reolink_aio
        AC_MSG_CHECKING([for Python reolink_aio module])
        if $PYTHON -c "import reolink_aio" 2>/dev/null; then
            have_python_reolink="yes"
            AC_MSG_RESULT([yes])
            AC_DEFINE([HAVE_PYTHON_REOLINK], [1], [Define if python reolink_aio is available])
        else
            AC_MSG_RESULT([no])
            AC_MSG_WARN([Python reolink_aio module not found. Install with: pip3 install reolink_aio])
        fi

        # Check for the MCP sidecar dependencies.
        #
        # T3.23: this used to be `import mcp, uvicorn, fastapi`, which only
        # proved the distributions were installed. It happily said "yes" with
        # mcp 2.0.0, where mcp.server.fastmcp no longer exists — so configure
        # enabled a sidecar that could not import its own entry point. A
        # configure that passes while the binary will not start is worse than
        # no check at all: it reassures.
        #
        # What follows is the set of third-party symbols the sidecar reaches
        # on its live import path, taken from
        # src/bin/calaos_mcp/python/calaos_mcp/*.py. Keep it in sync with those
        # sources; anything less is a guess.
        #
        # It does not stop at imports. server.py drives FastMCP through three
        # attributes that no import statement would reveal:
        #   - mcp.settings.transport_security  (server.py:64, module level)
        #   - FastMCP.streamable_http_app()    (server.py:167)
        #   - mcp._session_manager             (server.py:171) — PRIVATE
        # The last one is exactly the kind of API that disappears without
        # notice, which is what just happened to mcp.server.fastmcp. So the
        # probe replays that sequence rather than trusting it.
        #
        # pydantic is deliberately NOT probed: the only module importing it,
        # models.py, is imported by nothing (dead code, see FINDINGS). It is
        # still pinned in pyproject.toml as a transitive of mcp/fastapi, and a
        # missing pydantic would fail the fastapi import above anyway.
        if test "x$with_mcp" != "xno"; then
            AC_MSG_CHECKING([for the Python API the calaos_mcp sidecar uses])
            mcp_api_probe="from mcp.server.fastmcp import FastMCP; \
from mcp.server.transport_security import TransportSecuritySettings; \
from fastapi import FastAPI, Request, Response; \
from fastapi.responses import JSONResponse; \
from starlette.middleware.base import BaseHTTPMiddleware; \
from starlette.routing import Route; \
import uvicorn, websockets; \
probe = FastMCP('configure-probe'); \
probe.settings.transport_security = TransportSecuritySettings(enable_dns_rebinding_protection=False); \
assert probe.streamable_http_app().routes; \
assert probe._session_manager is not None"
            if $PYTHON -c "$mcp_api_probe" 2>/dev/null; then
                have_python_mcp="yes"
                AC_MSG_RESULT([yes])
                AC_DEFINE([HAVE_PYTHON_MCP], [1], [Define if Python MCP sidecar deps are available])
            else
                AC_MSG_RESULT([no])
                AC_MSG_WARN([calaos_mcp sidecar dependencies missing or incompatible.])
                AC_MSG_WARN([Failing import:])
                $PYTHON -c "$mcp_api_probe" 2>&1 | sed 's/^/    /' >&2
                AC_MSG_WARN([Install the pinned set declared in src/bin/calaos_mcp/pyproject.toml:])
                AC_MSG_WARN([  scripts/pyproject-requirements.py src/bin/calaos_mcp/pyproject.toml > req.txt])
                AC_MSG_WARN([  pip3 install -r req.txt --break-system-packages])
            fi
        fi
    else
        AC_MSG_WARN([pip3 not found. Install with: apt-get install python3-pip])
        have_python="no"
    fi
], [
    AC_MSG_WARN([Python 3 not found. Python support will be disabled])
])

AM_CONDITIONAL([HAVE_PYTHON], [test "x$have_python" = "xyes"])
AM_CONDITIONAL([HAVE_PYTHON_ROON], [test "x$have_python_roon" = "xyes"])
AM_CONDITIONAL([HAVE_PYTHON_REOLINK], [test "x$have_python_reolink" = "xyes"])
AM_CONDITIONAL([HAVE_PYTHON_MCP], [test "x$have_python_mcp" = "xyes"])

### AddressSanitizer (--enable-asan)
#Off by default: a plain ./configure must produce byte-for-byte the same flags
#as before this option existed, so nothing here runs unless it is asked for.
#
#This block sits deliberately at the very end, after every configure-time
#compile/link probe (AC_CHECK_LIB, PKG_CHECK_MODULES, EFL_CHECK_COMPILER_FLAGS,
#AX_CXX_COMPILE_STDCXX_20, ACX_PTHREAD...): appending -fsanitize=address to
#CFLAGS/CXXFLAGS/LDFLAGS earlier would make each of those probes compile and
#link against libasan, which can change their outcome (and would leak the
#sanitizer into the pkg-config/compiler-flag detection results cached in
#config.log). Only the flags handed to make need the sanitizer.
have_asan="no"
ASAN_HINT=""
AC_ARG_ENABLE([asan],
    AS_HELP_STRING([--enable-asan],
                   [build with AddressSanitizer (-fsanitize=address), for debugging only @<:@default=no@:>@]),
    [enable_asan=$enableval],
    [enable_asan=no])

if test "x${enable_asan}" = "xyes"
then
    have_asan="yes"
    #-O1 + frame pointers: ASan needs readable stack traces, and -O0 makes the
    #test suite crawl. -g is repeated here on purpose so the option works even
    #if the caller overrode CFLAGS/CXXFLAGS without it.
    ASAN_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g -O1"
    CFLAGS="${CFLAGS} ${ASAN_FLAGS}"
    CXXFLAGS="${CXXFLAGS} ${ASAN_FLAGS}"
    LDFLAGS="${LDFLAGS} ${ASAN_FLAGS}"
    ASAN_HINT="-- run the suite as: ASAN_OPTIONS=detect_leaks=0 make check"
    AC_MSG_NOTICE([AddressSanitizer enabled: ${ASAN_FLAGS}])
fi

AC_CONFIG_FILES([
Makefile
tests/Makefile
src/Makefile
src/lib/Makefile
src/lib/calaos-python/Makefile
src/bin/Makefile
src/bin/calaos_server/Makefile
src/bin/calaos_mcp/Makefile
src/bin/tools/Makefile
data/Makefile
data/debug/Makefile
data/debug/dist/Makefile
src/bin/calaos_server/Audio/calaos_roon
src/bin/calaos_server/IO/Reolink/calaos_reolink
src/bin/calaos_mcp/calaos_mcp
])

# po/Makefile.in must be listed literally, not through a shell variable: config.status
# stores the AC_CONFIG_FILES arguments as case patterns, and a "$var" pattern is empty
# when config.status is re-invoked standalone -- which is exactly what automake's
# "po/Makefile: ... POTFILES.in" rule does whenever POTFILES.in is edited. The gettext
# condition below is an m4-time test, identical to the one guarding AM_GNU_GETTEXT above.
m4_ifdef([AM_GNU_GETTEXT], [AC_CONFIG_FILES([po/Makefile.in])])

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
echo "  AddressSanitizer (--enable-asan).....: $(colorOut $have_asan) $ASAN_HINT"
echo
echo "Installation...........................: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............................: $prefix"
echo
echo "Dependencies:"
echo "  UrlDownloader picture resizing.......: $(colorOut $CONVERT_INFO)"
echo "  pugixml (XML + XPath 1.0)............: $PUGIXML_INFO"
echo
echo "Hardware support:"
echo "  One Wire support.....................: $(colorOut $have_owcapi)"
echo "  Open Lightning Architecture support..: $(colorOut $have_libola)"
echo "  Eib/KNX support (eibd or knxd).......: $(colorOut $have_libknx)"
echo "  MQTT support (libmosquittopp)........: $(colorOut $have_mosquitto)"
echo
echo "Python support:........................: $(colorOut $have_python)"
echo "  RoonAPI support......................: $(colorOut $have_python_roon)"
echo "  Reolink support......................: $(colorOut $have_python_reolink)"
echo "  MCP sidecar..........................: $(colorOut $have_python_mcp)"
echo
