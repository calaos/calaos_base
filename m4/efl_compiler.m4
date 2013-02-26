dnl Copyright (C) 2012 Vincent Torri <vincent dot torri at gmail dot com>
dnl This code is public domain and can be freely used or copied.

dnl Macro that check if compiler of linker flags are available


dnl Macro that checks for a compiler flag availability
dnl
dnl _EFL_CHECK_COMPILER_FLAGS(EFL, FLAGS)
dnl AC_SUBST : EFL_CFLAGS (EFL being replaced by its value)
dnl have_flag: yes or no.
AC_DEFUN([_EFL_CHECK_COMPILER_FLAGS],
[dnl
m4_pushdef([UPEFL], m4_translit([[$1]], [-a-z], [_A-Z]))dnl

dnl store in options -Wfoo if -Wno-foo is passed
option="m4_bpatsubst([[$2]], [-Wno-], [-W])"
CFLAGS_save="${CFLAGS}"
CFLAGS="${CFLAGS} ${option}"
AC_LANG_PUSH([C])

AC_MSG_CHECKING([whether the compiler supports $2])
AC_COMPILE_IFELSE(
   [AC_LANG_PROGRAM([[]])],
   [have_flag="yes"],
   [have_flag="no"])
AC_MSG_RESULT([${have_flag}])

AC_LANG_POP([C])
CFLAGS="${CFLAGS_save}"
if test "x${have_flag}" = "xyes" ; then
   UPEFL[_CFLAGS]="${UPEFL[_CFLAGS]} [$2]"
fi
AC_SUBST(UPEFL[_CFLAGS])dnl
m4_popdef([UPEFL])dnl
])

dnl EFL_CHECK_COMPILER_FLAGS(EFL, FLAGS)
dnl Checks if FLAGS are supported and add to EFL_CLFAGS.
dnl
dnl It will first try every flag at once, if one fails will try them one by one.
AC_DEFUN([EFL_CHECK_COMPILER_FLAGS],
[dnl
_EFL_CHECK_COMPILER_FLAGS([$1], [$2])
if test "${have_flag}" != "yes"; then
m4_foreach_w([flag], [$2], [_EFL_CHECK_COMPILER_FLAGS([$1], m4_defn([flag]))])
fi
])


dnl Macro that checks for a linker flag availability
dnl
dnl _EFL_CHECK_LINKER_FLAGS(EFL, FLAGS)
dnl AC_SUBST : EFL_LDFLAGS (EFL being replaced by its value)
dnl have_flag: yes or no
AC_DEFUN([_EFL_CHECK_LINKER_FLAGS],
[dnl
m4_pushdef([UPEFL], m4_translit([[$1]], [-a-z], [_A-Z]))dnl

LDFLAGS_save="${LDFLAGS}"
LDFLAGS="${LDFLAGS} $2"
AC_LANG_PUSH([C])

AC_MSG_CHECKING([whether the linker supports $2])
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM([[]])],
   [have_flag="yes"],
   [have_flag="no"])
AC_MSG_RESULT([${have_flag}])

AC_LANG_POP([C])
LDFLAGS="${LDFLAGS_save}"
if test "x${have_flag}" = "xyes" ; then
   UPEFL[_LDFLAGS]="${UPEFL[_LDFLAGS]} [$2]"
fi
AC_SUBST(UPEFL[_LDFLAGS])dnl
m4_popdef([UPEFL])dnl
])

dnl EFL_CHECK_LINKER_FLAGS(EFL, FLAGS)
dnl Checks if FLAGS are supported and add to EFL_CLFAGS.
dnl
dnl It will first try every flag at once, if one fails will try them one by one.
AC_DEFUN([EFL_CHECK_LINKER_FLAGS],
[dnl
_EFL_CHECK_LINKER_FLAGS([$1], [$2])
if test "${have_flag}" != "yes"; then
m4_foreach_w([flag], [$2], [_EFL_CHECK_LINKER_FLAGS([$1], m4_defn([flag]))])
fi
])dnl
