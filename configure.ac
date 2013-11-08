AC_INIT([calaos], [2.0], [raoul.hecky@gmail.com])
AC_PREREQ([2.59])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.6 dist-bzip2 subdir-objects])
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
AX_CXX_COMPILE_STDCXX_11([noext],[mandatory])

# pkg-config
PKG_PROG_PKG_CONFIG

### Checks for libraries

efl_ver="1.7.0"

requirements_calaos_common="eina >= ${efl_ver} eet >= ${efl_ver} ecore >= ${efl_ver} ecore-file >= ${efl_ver} ecore-con >= ${efl_ver} sigc++-2.0 >= 2.2.0 log4cpp >= 1.0 jansson >= 2.0.0 vmime >= 0.9.0 lua5.1 >= 5.1.0"
PKG_CHECK_MODULES([CALAOS_COMMON], [${requirements_calaos_common}])
AC_SUBST([requirements_calaos_common])

requirements_calaos_server="eina >= ${efl_ver} eet >= ${efl_ver} ecore >= ${efl_ver} ecore-file >= ${efl_ver} ecore-con >= ${efl_ver} libcurl"
PKG_CHECK_MODULES([CALAOS_SERVER], [${requirements_calaos_server}])
AC_SUBST([requirements_calaos_server])

requirements_calaos_home="eina >= ${efl_ver} eet >= ${efl_ver} ecore >= ${efl_ver} ecore-file >= ${efl_ver} ecore-con >= ${efl_ver} elementary >= ${efl_ver} ecore-x >= ${efl_ver} openssl >= 1.0.0 libcurl"
PKG_CHECK_MODULES([CALAOS_HOME], [${requirements_calaos_home}], [have_calaos_home_dep="yes"], [have_calaos_home_dep="no"])
AM_CONDITIONAL([HAVE_CALAOS_HOME_BIN], [test "x${have_calaos_home_dep}" = "xyes"])

EFL_CHECK_FUNCS([CALAOS_HOME], [dlopen])


## Theme
AC_ARG_ENABLE([theme],
   [AC_HELP_STRING(
       [--disable-theme],
       [disable theme. @<:@default=enabled@:>@])],
   [
    if test "x${enableval}" = "xyes"; then
       build_theme="yes"
    else
       build_theme="no"
    fi
   ],
   [build_theme="yes"])

AM_CONDITIONAL([CALAOS_BUILD_THEME], [test "x${build_theme}" = "xyes"])

AC_MSG_CHECKING([whether Theme is to be built])
AC_MSG_RESULT([${build_theme}])

AM_CONDITIONAL([ENNA_BUILD_THEME], [test "x${build_theme}" = "xyes"])
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
    AC_CHECK_LIB([owcapi], [OW_init],, AC_ERROR([Misssing owfs library]))
fi

AC_CONFIG_FILES([
Makefile
src/Makefile
src/lib/Makefile
src/bin/Makefile
src/bin/calaos_server/Makefile
src/bin/calaos_home/Makefile
src/widgets/Makefile
src/widgets/clock/Makefile
data/Makefile
data/themes/Makefile
data/themes/default/Makefile
data/themes/widgets/Makefile
data/themes/widgets/clock/Makefile
po/Makefile.in
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
echo " Build theme.......... : $build_theme"
echo
echo " One Wire support......: ${have_owcapi}"
echo
