ACLOCAL_AMFLAGS = -I m4

SUBDIRS = po src data

MAINTAINERCLEANFILES = \
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
missing \
$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.gz \
$(PACKAGE_TARNAME)-$(PACKAGE_VERSION).tar.bz2 \
m4/libtool.m4 \
m4/lt~obsolete.m4 \
m4/ltoptions.m4 \
m4/ltsugar.m4 \
m4/ltversion.m4 \
m4/codeset.m4 \
m4/fcntl-o.m4 \
m4/gettext.m4* \
m4/glibc2*.m4 \
m4/iconv.m4 \
m4/intdiv0.m4 \
m4/intldir.m4 \
m4/intl.m4 \
m4/intlmacosx.m4 \
m4/intmax.m4* \
m4/inttypes_h.m4 \
m4/inttypes.m4 \
m4/inttypes-pri.m4 \
m4/isc-posix.m4 \
m4/lcmessage.m4 \
m4/lib-ld.m4* \
m4/lib-link.m4 \
m4/lib-prefix.m4* \
m4/lock.m4 \
m4/longdouble.m4* \
m4/longlong.m4* \
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
po/boldquot.sed \
po/calaos.pot \
po/en@boldquot.header \
po/en@boldquot.po \
po/en@quot.header \
po/en@quot.po \
po/insert-header.sin \
po/Makefile.in.in \
po/Makevars.template \
po/quot.sed \
po/remove-potcdate.sin \
po/Rules-quot


EXTRA_DIST = \
AUTHORS \
COPYING \
README \
autogen.sh \
m4/efl_compiler.m4 \
src/lib/gettext.h

check-gettext:
	@if test x$(USE_NLS) != "xyes" ; then echo "Missing gettext. Rerun configure and check for" \
	"'checking whether to use NLS... yes'!" ; exit 1 ; fi

update-po: check-gettext
	@find $(srcdir)/src/ -name "*.cpp" -print | sort > $(srcdir)/po/POTFILES.in.2 ; \
	if diff $(srcdir)/po/POTFILES.in $(srcdir)/po/POTFILES.in.2 >/dev/null 2>&1 ; then \
		rm -f $(srcdir)/po/POTFILES.in.2 ; \
	else \
		mv $(srcdir)/po/POTFILES.in.2 $(srcdir)/po/POTFILES.in ; \
	fi
	cd po && $(MAKE) $(AM_MAKEFLAGS) update-po

update-gmo: check-gettext
	cd po && $(MAKE) $(AM_MAKEFLAGS) update-gmo

force-update-gmo: check-gettext
	touch po/*.po
	cd po && $(MAKE) $(AM_MAKEFLAGS) update-gmo

force-update-gmo-%: check-gettext
	@language=`echo $@ | sed s/force-update-gmo-//` ; \
	if test ! -f po/$$language.po ; then echo "file po/$$language.po does not exist" ; exit 1 ; fi ; \
	touch po/$$language.po ; \
	cd po && $(MAKE) $(AM_MAKEFLAGS) update-gmo

.PHONY: check-gettext update-po update-gmo force-update-gmo
