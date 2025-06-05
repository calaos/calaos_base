#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

#AC_PREREQ([2.63])
AC_INIT(libquickmail, 0.1.29)
AM_INIT_AUTOMAKE(libquickmail, 0.1.29)

# for Windows DLL support call AC_LIBTOOL_WIN32_DLL before AC_PROG_LIBTOOL
AC_LIBTOOL_WIN32_DLL
#LT_INIT
LT_INIT([shared static])
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([quickmail.c])

AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
#AC_PROG_CXX

# Checks for libraries.
AC_HAVE_LIBRARY(curl)
#PKG_CHECK_MODULES([CURL], [libcurl >= 7.20.0])

# Checks for header files.
AC_CHECK_HEADERS([stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memmove strdup])

# Allow compiling with per-target flags
AM_PROG_CC_C_O

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

if test x$target_os = xmingw32; then
  WINSOCK=-lws2_32
fi
AC_SUBST([WINSOCK])

#autoreconf -f -i -I m4 -I $MINGWPREFIX/share/aclocal && ./configure && make
