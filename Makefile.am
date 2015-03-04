ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src data tests

if HAVE_PO

SUBDIRS += po

endif

MAINTAINERCLEANFILES = \
$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.bz2 \
$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.gz \
$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.xz \
Makefile.in \
aclocal.m4 \
config.guess \
config.h.in \
config.h.in~ \
config.rpath \
config.sub \
configure \
depcomp \
install-sh \
ltconfig \
ltmain.sh \
m4/codeset.m4 \
m4/fcntl-o.m4 \
m4/gettext.m4* \
m4/glibc2*.m4 \
m4/iconv.m4 \
m4/intdiv0.m4 \
m4/intl.m4 \
m4/intldir.m4 \
m4/intlmacosx.m4 \
m4/intmax.m4* \
m4/inttypes-pri.m4 \
m4/inttypes.m4 \
m4/inttypes_h.m4 \
m4/isc-posix.m4 \
m4/lcmessage.m4 \
m4/lib-ld.m4* \
m4/lib-link.m4 \
m4/lib-prefix.m4* \
m4/libtool.m4 \
m4/lock.m4 \
m4/longdouble.m4* \
m4/longlong.m4* \
m4/ltoptions.m4 \
m4/ltsugar.m4 \
m4/ltversion.m4 \
m4/lt~obsolete.m4 \
m4/nls.m4 \
m4/po.m4* \
m4/printf-posix.m4* \
m4/progtest.m4 \
m4/signed.m4 \
m4/size_max.m4* \
m4/stdint_h.m4 \
m4/threadlib.m4 \
m4/uintmax_t.m4 \
m4/ulonglong.m4* \
m4/visibility.m4 \
m4/wchar_t.m4 \
m4/wint_t.m4* \
m4/xsize.m4* \
missing

if HAVE_PO

MAINTAINERCLEANFILES += \
po/Makefile.in.in \
po/Makevars.template \
po/Rules-quot \
po/boldquot.sed \
po/calaos.pot \
po/en@boldquot.header \
po/en@boldquot.po \
po/en@quot.header \
po/en@quot.po \
po/insert-header.sin \
po/quot.sed \
po/remove-potcdate.sin

endif

EXTRA_DIST = \
AUTHORS \
COPYING \
README.md \
autogen.sh \
m4/efl_compiler.m4
