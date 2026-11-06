# =============================================================================
#  https://www.gnu.org/software/autoconf-archive/ax_cxx_compile_stdcxx.html
# =============================================================================
#
# SYNOPSIS
#
#   AX_CXX_COMPILE_STDCXX_20([ext|noext], [mandatory|optional])
#
# DESCRIPTION
#
#   Check for baseline language coverage in the compiler for the C++20
#   standard; if necessary, add switches to CXX and CXXCPP to enable
#   support.
#
#   This macro is a convenience alias for calling the AX_CXX_COMPILE_STDCXX
#   macro with the version set to C++20.  The two optional arguments are
#   forwarded literally as the second and third argument respectively.
#   Please see the documentation for the AX_CXX_COMPILE_STDCXX macro for
#   more information.  If you want to use this macro, you also need to
#   download the ax_cxx_compile_stdcxx.m4 file.
#
#   NOTE: as of this writing, the GNU Autoconf Archive only ships the
#   convenience wrappers up to AX_CXX_COMPILE_STDCXX_17; there is no
#   published ax_cxx_compile_stdcxx_20.m4 yet. This file is a local wrapper
#   that follows the exact same pattern as the official
#   ax_cxx_compile_stdcxx_{11,14,17}.m4 files, forwarding to the base
#   AX_CXX_COMPILE_STDCXX macro (which already supports "20" as a first
#   argument in the vendored ax_cxx_compile_stdcxx.m4 in this directory).
#
# LICENSE
#
#   Copyright (c) 2015 Moritz Klammler <moritz@klammler.eu>
#   Copyright (c) 2016 Krzesimir Nowak <qdlacz@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

AX_REQUIRE_DEFINED([AX_CXX_COMPILE_STDCXX])
AC_DEFUN([AX_CXX_COMPILE_STDCXX_20], [AX_CXX_COMPILE_STDCXX([20], [$1], [$2])])
