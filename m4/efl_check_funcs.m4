dnl Copyright (C) 2012 Vincent Torri <vincent dot torri at gmail dot com>
dnl This code is public domain and can be freely used or copied.

dnl Macros that check functions availability for the EFL:

dnl dirfd
dnl dladdr
dnl dlopen
dnl fnmatch
dnl iconv
dnl setxattr (an al.)
dnl shm_open


dnl _EFL_CHECK_FUNC_DIRFD is for internal use
dnl _EFL_CHECK_FUNC_DIRFD(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_DIRFD],
[
AC_LINK_IFELSE(
   [
    AC_LANG_PROGRAM(
       [[
#ifdef HAVE_DIRENT_H
# include <dirent.h>
#endif
       ]],
       [[
int main(void)
{
  DIR *dirp;
  return dirfd(dirp);
}
       ]])
   ],
   [_efl_have_fct="yes"],
   [_efl_have_fct="no"])

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])
])

dnl _EFL_CHECK_FUNC_DLADDR_PRIV is for internal use
dnl _EFL_CHECK_FUNC_DLADDR_PRIV(EFL, LIB, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_DLADDR_PRIV],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

LIBS_save="${LIBS}"
LIBS="${LIBS} $2"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#define _GNU_SOURCE
#include <dlfcn.h>
         ]],
         [[
int res = dladdr(0, 0);
       ]])],
   [
    m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} $2"
    requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} $2"
    _efl_have_fct="yes"
   ],
   [_efl_have_fct="no"])

LIBS="${LIBS_save}"

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$3], [$4])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_DLADDR is for internal use
dnl _EFL_CHECK_FUNC_DLADDR(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_DLADDR],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

case "$host_os" in
   mingw*)
      _efl_have_fct="yes"
      requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} -ldl"
      m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} -ldl"
   ;;
   *)
      _efl_have_fct="no"

dnl Check is dladdr is in libc
      _EFL_CHECK_FUNC_DLADDR_PRIV([$1], [], [_efl_have_fct="yes"], [_efl_have_fct="no"])

dnl Check is dlopen is in libdl
      if test "x${_efl_have_fct}" = "xno" ; then
         _EFL_CHECK_FUNC_DLADDR_PRIV([$1], [-ldl], [_efl_have_fct="yes"], [_efl_have_fct="no"])
      fi
   ;;
esac

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_DLOPEN_PRIV is for internal use
dnl _EFL_CHECK_FUNC_DLOPEN_PRIV(EFL, LIB, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_DLOPEN_PRIV],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

LIBS_save="${LIBS}"
LIBS="${LIBS} $2"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#include <dlfcn.h>
         ]],
         [[
void *h = dlopen(0, 0);
       ]])],
   [
    m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} $2"
    requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} $2"
    _efl_have_fct="yes"
   ],
   [_efl_have_fct="no"])

LIBS="${LIBS_save}"

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$3], [$4])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_DLOPEN is for internal use
dnl _EFL_CHECK_FUNC_DLOPEN(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_DLOPEN],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

case "$host_os" in
   mingw*)
      _efl_have_fct="yes"
      requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} -ldl"
      m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} -ldl"
   ;;
   *)
      _efl_have_fct="no"

dnl Check is dlopen is in libc
      _EFL_CHECK_FUNC_DLOPEN_PRIV([$1], [], [_efl_have_fct="yes"], [_efl_have_fct="no"])

dnl Check is dlopen is in libdl
      if test "x${_efl_have_fct}" = "xno" ; then
         _EFL_CHECK_FUNC_DLOPEN_PRIV([$1], [-ldl], [_efl_have_fct="yes"], [_efl_have_fct="no"])
      fi
   ;;
esac

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_FNMATCH_PRIV is for internal use
dnl _EFL_CHECK_FUNC_FNMATCH_PRIV(EFL, LIB, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_FNMATCH_PRIV],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

LIBS_save="${LIBS}"
LIBS="${LIBS} $2"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#include <stdlib.h>
#include <fnmatch.h>
         ]],
         [[
int g = fnmatch(NULL, NULL, 0);
       ]])],
   [
    m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} $2"
    requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} $2"
    _efl_have_fct="yes"
   ],
   [_efl_have_fct="no"])

LIBS="${LIBS_save}"

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$3], [$4])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_FNMATCH is for internal use
dnl _EFL_CHECK_FUNC_FNMATCH(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_FNMATCH],
[
case "$host_os" in
   mingw*)
      _efl_have_fct="yes"
   ;;
   *)
dnl Check is fnmatch is in libc
      _EFL_CHECK_FUNC_FNMATCH_PRIV([$1], [], [_efl_have_fct="yes"], [_efl_have_fct="no"])

dnl Check is fnmatch is in libfnmatch
      if test "x${_efl_have_fct}" = "xno" ; then
         _EFL_CHECK_FUNC_FNMATCH_PRIV([$1], [-lfnmatch], [_efl_have_fct="yes"], [_efl_have_fct="no"])
      fi

dnl Check is fnmatch is in libiberty
      if test "x${_efl_have_fct}" = "xno" ; then
         _EFL_CHECK_FUNC_FNMATCH_PRIV([$1], [-liberty], [_efl_have_fct="yes"], [_efl_have_fct="no"])
      fi
   ;;
esac

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])
])

dnl _EFL_CHECK_FUNC_ICONV_PRIV is for internal use
dnl _EFL_CHECK_FUNC_ICONV_PRIV(EFL, LIB, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_ICONV_PRIV],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

LIBS_save="${LIBS}"
LIBS="${LIBS} $2"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#include <stdlib.h>
#include <iconv.h>
         ]],
         [[
iconv_t ic;
size_t count = iconv(ic, NULL, NULL, NULL, NULL);
       ]])],
   [
    m4_defn([UPEFL])[]_LIBS="${m4_defn([UPEFL])[]_LIBS} $2"
    requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} $2"
    _efl_have_fct="yes"
   ],
   [_efl_have_fct="no"])

LIBS="${LIBS_save}"

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$3], [$4])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_ICONV is for internal use
dnl _EFL_CHECK_FUNC_ICONV(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_ICONV],
[
AC_ARG_WITH([iconv-link],
   AC_HELP_STRING([--with-iconv-link=ICONV_LINK], [explicitly specify an iconv link option]),
   [
    _efl_have_fct="yes"
    iconv_libs=${withval}
   ],
   [_efl_have_fct="no"])

AC_MSG_CHECKING([for explicit iconv link options])
if test "x${iconv_libs}" = "x" ; then
   AC_MSG_RESULT([no explicit iconv link option])
else
   AC_MSG_RESULT([${iconv_libs}])
fi

dnl Check is iconv is in libc
if test "x${_efl_have_fct}" = "xno" ; then
   _EFL_CHECK_FUNC_ICONV_PRIV([$1], [], [_efl_have_fct="yes"], [_efl_have_fct="no"])
fi

dnl Check is iconv is in libiconv
if test "x${_efl_have_fct}" = "xno" ; then
   _EFL_CHECK_FUNC_ICONV_PRIV([$1], [-liconv], [_efl_have_fct="yes"], [_efl_have_fct="no"])
fi

dnl Check is iconv is in libiconv_plug
if test "x${_efl_have_fct}" = "xno" ; then
   _EFL_CHECK_FUNC_ICONV_PRIV([$1], [-liconv_plug], [_efl_have_fct="yes"], [_efl_have_fct="no"])
fi

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])
])

dnl _EFL_CHECK_FUNC_SETXATTR is for internal use
dnl _EFL_CHECK_FUNC_SETXATTR(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_SETXATTR],
[
AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#include <stdlib.h>
#include <sys/types.h>
#include <sys/xattr.h>
       ]],
       [[
size_t tmp = listxattr("/", NULL, 0);
tmp = getxattr("/", "user.ethumb.md5", NULL, 0);
setxattr("/", "user.ethumb.md5", NULL, 0, 0);
       ]])],
   [_efl_have_fct="yes"],
   [_efl_have_fct="no"])

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])
])

dnl _EFL_CHECK_FUNC_SHM_OPEN_PRIV is for internal use
dnl _EFL_CHECK_FUNC_SHM_OPEN_PRIV(EFL, LIB, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_SHM_OPEN_PRIV],
[
m4_pushdef([UPEFL], m4_translit([$1], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWNEFL], m4_translit([$1], [-A-Z], [_a-z]))dnl

LIBS_save="${LIBS}"
LIBS="${LIBS} $2"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
       [[
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
       ]],
       [[
int fd;

fd = shm_open("/dev/null", O_RDONLY, S_IRWXU | S_IRWXG | S_IRWXO);
       ]])],
   [
    m4_defn([UPEFL])[]_LIBS="$m4_defn([UPEFL])[]_LIBS $2"
    requirements_libs_[]m4_defn([DOWNEFL])="${requirements_libs_[]m4_defn([DOWNEFL])} $2"
    _efl_have_fct="yes"
   ],
   [_efl_have_fct="no"])

LIBS="${LIBS_save}"

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$3], [$4])

m4_popdef([DOWNEFL])
m4_popdef([UPEFL])
])

dnl _EFL_CHECK_FUNC_SHM_OPEN is for internal use
dnl _EFL_CHECK_FUNC_SHM_OPEN(EFL, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)

AC_DEFUN([_EFL_CHECK_FUNC_SHM_OPEN],
[
_efl_have_fct="no"

dnl Check is shm_open is in libc
_EFL_CHECK_FUNC_SHM_OPEN_PRIV([$1], [],
   [_efl_have_fct="yes"],
   [_efl_have_fct="no"])

dnl Check is shm_open is in librt
if test "x${_efl_have_fct}" = "xno" ; then
   _EFL_CHECK_FUNC_SHM_OPEN_PRIV([$1], [-lrt],
      [_efl_have_fct="yes"],
      [_efl_have_fct="no"])
fi

AS_IF([test "x${_efl_have_fct}" = "xyes"], [$2], [$3])
])

dnl Macro that checks function availability
dnl
dnl EFL_CHECK_FUNC(EFL, FUNCTION)
dnl AC_SUBST : EFL_CFLAGS and EFL_LIBS (EFL being replaced by its value)
dnl AC_DEFINE : EFL_HAVE_FUNCTION (FUNCTION being replaced by its value)
dnl result in efl_func_function (function being replaced by its value)

AC_DEFUN([EFL_CHECK_FUNC],
[
m4_pushdef([UP], m4_translit([$2], [-a-z], [_A-Z]))dnl
m4_pushdef([DOWN], m4_translit([$2], [-A-Z], [_a-z]))dnl

m4_default([_EFL_CHECK_FUNC_]m4_defn([UP]))($1, [have_fct="yes"], [have_fct="no"])

if test "x$2" = "xsetxattr" ; then
   AC_MSG_CHECKING([for extended attributes])
else
   AC_MSG_CHECKING([for ]m4_defn([DOWN]))
fi

AC_MSG_RESULT([${have_fct}])

if test "x${have_fct}" = "xyes" ; then
   if test "x$2" = "xsetxattr" ; then
      AC_DEFINE([HAVE_XATTR], [1], [Define to 1 if you have the `listxattr', `setxattr' and `getxattr' functions.])
   else
      AC_DEFINE([HAVE_]m4_defn([UP]), [1], [Define to 1 if you have the `]m4_defn([DOWN])[' function.])
   fi
fi

efl_func_[]m4_defn([DOWN])="${have_fct}"

m4_popdef([DOWN])
m4_popdef([UP])
])

dnl Macro that iterates over a sequence of space separated functions
dnl and that calls EFL_CHECK_FUNC() for each of these functions
dnl
dnl EFL_CHECK_FUNCS(EFL, FUNCTIONS)

AC_DEFUN([EFL_CHECK_FUNCS],
[
m4_foreach_w([fct], [$2], [EFL_CHECK_FUNC($1, m4_defn([fct]))])
])
