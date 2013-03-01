AC_INIT([calaos], [2.0], [raoul.hecky@gmail.com])
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.6 dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_GNU_SOURCE
AC_SYS_LARGEFILE

LT_PREREQ([2.2])
LT_INIT([win32-dll])

AM_GNU_GETTEXT_VERSION([0.17])

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

# pkg-config
PKG_PROG_PKG_CONFIG

### Checks for libraries

requirements_calaos_common="eina >= 1.7.0 eet >= 1.7.0 ecore >= 1.7.0 ecore-file >= 1.7.0 ecore-con >= 1.7.0 sigc++-2.0 >= 2.2.0 log4cpp >= 1.0 jansson >= 2.0.0 vmime >= 0.9.0 lua5.1 >= 5.1.0"
PKG_CHECK_MODULES([CALAOS_COMMON], [${requirements_calaos_common}])
AC_SUBST([requirements_calaos_common])

requirements_calaos_server="eina >= 1.7.0 eet >= 1.7.0 ecore >= 1.7.0 ecore-file >= 1.7.0 ecore-con >= 1.7.0"
PKG_CHECK_MODULES([CALAOS_SERVER], [${requirements_calaos_server}])
AC_SUBST([requirements_calaos_server])

requirements_calaos_home="eina >= 1.7.0 eet >= 1.7.0 ecore >= 1.7.0 ecore-file >= 1.7.0 ecore-con >= 1.7.0 elementary >= 1.7.0 ecore-x >= 1.7.0 openssl >= 1.0.0"
PKG_CHECK_MODULES([CALAOS_HOME], [${requirements_calaos_home}], [have_calaos_home_dep="yes"], [have_calaos_home_dep="no"])
AM_CONDITIONAL([HAVE_CALAOS_HOME_BIN], [test "x${have_calaos_home_dep}" = "xyes"])

### Checks for compiler characteristics
AC_C_BIGENDIAN
AC_C_INLINE

EFL_CHECK_COMPILER_FLAGS([CALAOS_COMMON], [-Wall -Wextra -Wno-unused-parameter -Wshadow -Wpointer-arith -Wno-missing-field-initializers -fvisibility=hidden -fdata-sections -ffunction-sections])

### Checks for header files
AC_HEADER_ASSERT
AC_CHECK_HEADERS([unistd.h])

AC_CONFIG_FILES([ po/Makefile.in
Makefile
src/Makefile
src/lib/Makefile
src/bin/Makefile
src/bin/calaos_server/Makefile
src/bin/calaos_home/Makefile
data/Makefile
data/themes/Makefile
data/themes/default/Makefile
])

AC_OUTPUT

#####################################################################
## Info

echo
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo
echo " Build Calaos Server...: yes"
echo " Build Calaos Home.....: $have_calaos_home_dep"
echo
