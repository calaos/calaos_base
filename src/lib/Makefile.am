SUBDIRS = calaos-python

AM_CXXFLAGS = \
        $(CXX_PIC) \
        -DETC_DIR=\"$(sysconfdir)\" \
        -DPACKAGE_BIN_DIR=\"$(bindir)\" \
        -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\" \
        -DPACKAGE_LIB_DIR=\"$(libdir)\" \
        -DPACKAGE_LOCALE_DIR=\"$(datadir)/locale\" \
        -I$(top_srcdir)/src/bin/calaos_server \
        -I$(top_srcdir)/src/bin/calaos_server/Audio \
        -I$(top_srcdir)/src/bin/calaos_server/IO \
        -I$(top_srcdir)/src/bin/calaos_server/IO/libmbus \
        -I$(top_srcdir)/src/bin/calaos_server/IPCam \
        -I$(top_srcdir)/src/bin/calaos_server/Rules \
        -I$(top_srcdir)/src/lib \
        -I$(top_srcdir)/src/lib/llhttp/include \
        -I$(top_srcdir)/src/lib/libquickmail \
        -I$(top_srcdir)/src/lib/uri_parser \
        -I. \
        -I$(top_srcdir)/src/lib/uvw/src \
        -I$(top_srcdir)/src/lib/sqlite_modern_cpp/hdr \
        -I$(top_srcdir)/src/lib/sole \
        -I$(top_srcdir)/src/lib/exprtk \
        @CALAOS_COMMON_CFLAGS@

AM_CFLAGS = \
        -DETC_DIR=\"$(sysconfdir)\" \
        -DPACKAGE_BIN_DIR=\"$(bindir)\" \
        -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\" \
        -DPACKAGE_LIB_DIR=\"$(libdir)\" \
        -DPACKAGE_LOCALE_DIR=\"$(datadir)/locale\" \
        -I$(top_srcdir)/src/bin/calaos_server \
        -I$(top_srcdir)/src/bin/calaos_server/Audio \
        -I$(top_srcdir)/src/bin/calaos_server/IO \
        -I$(top_srcdir)/src/bin/calaos_server/IO/libmbus \
        -I$(top_srcdir)/src/bin/calaos_server/IPCam \
        -I$(top_srcdir)/src/bin/calaos_server/Rules \
        -I$(top_srcdir)/src/lib \
        -I$(top_srcdir)/src/lib/LuaScript \
        -I$(top_srcdir)/src/lib/llhttp/include \
        -I$(top_srcdir)/src/lib/libquickmail \
        -I$(top_srcdir)/src/lib/uri_parser \
        -I. \
        @CALAOS_COMMON_CFLAGS@


noinst_LTLIBRARIES = \
        libcalaos_common.la

libcalaos_common_la_SOURCES = \
        CalaosModule.h                          \
        Calendar.cpp                            \
        Calendar.h                              \
        ConfigOptions.cpp                       \
        ConfigOptions.h                         \
        ExpressionEvaluator.h                   \
        ExpressionEvaluator.cpp                 \
        Timer.cpp                               \
        Timer.h                                 \
        Logger.cpp                              \
        Logger.h                                \
        Jansson_Addition.h                      \
        Json_Addition.h                         \
        Params.cpp                              \
        Params.h                                \
        Prefix.cpp                              \
        Prefix.h                                \
        TimeRange.cpp                           \
        TimeRange.h                             \
        UrlDownloader.cpp                       \
        UrlDownloader.h                         \
        Constants.h                             \
        MemMacros.h                             \
        LogSetup.cpp                            \
        LogSetup.h                              \
        StringUtils.cpp                         \
        StringUtils.h                           \
        ConfigStore.cpp                         \
        ConfigStore.h                           \
        SystemInfo.cpp                          \
        SystemInfo.h                            \
        Utils.cpp                               \
        Utils.h                                 \
        XmlUtils.h                              \
        WebSocketFrame.cpp                      \
        WebSocketFrame.h                        \
        base64.cpp                              \
        base64.h                                \
        llhttp/src/api.c                        \
        llhttp/src/http.c                       \
        llhttp/src/llhttp.c                     \
        llhttp/include/llhttp.h                 \
        libquickmail/quickmail.c                \
        libquickmail/quickmail.h                \
        sunset.c                                \
        sunset.h                                \
        tcpsocket.cpp                           \
        tcpsocket.h                             \
        uri_parser/hef_uri_syntax.cpp           \
        uri_parser/hef_uri_syntax.h             \
        ColorUtils.h                            \
        ColorUtils.cpp                          \
        json.hpp                                \
        uvw/src/uvw.hpp                         \
        FileUtils.h                             \
        FileUtils.cpp                           \
        ThreadedQueue.h                         \
        sole/sole.hpp                           \
        libuvw.h

libcalaos_common_la_LIBADD = -lm @CALAOS_COMMON_LIBS@

# cpp-tui is a header-only vendored library: nothing to compile, but it must
# be shipped by "make dist". See cpptui/README.calaos for provenance.
EXTRA_DIST = \
        cpptui/cpptui.hpp                       \
        cpptui/LICENSE                          \
        cpptui/README.calaos

# T3.48: sigc_fix_functor.h is compiled by nothing -- no source includes it --
# but po/POTFILES.in:355 names it, and xgettext dies on a POTFILES entry that
# is not in the tarball. Shipped rather than deleted: removing a header is a
# source change, not a packaging one, and POTFILES.in would have to follow.
EXTRA_DIST += sigc_fix_functor.h

MAINTAINERCLEANFILES = Makefile.in

# E4.4a
#pugixml (MIT, https://pugixml.org/) -- XML DOM + XPath 1.0. configure prefers
#the distro package; when it is absent it points PUGIXML_CFLAGS at the vendored
#copy below and the three upstream files are compiled straight into
#libcalaos_common. Both branches reach consumers through @CALAOS_COMMON_CFLAGS@ /
#@CALAOS_COMMON_LIBS@ (and their CALAOS_SERVER twins), set in configure.ac.
#Upstream sources are kept verbatim, licence header included: do not edit them.
if HAVE_PUGIXML_SYSTEM
EXTRA_DIST += \
        pugixml/pugiconfig.hpp                  \
        pugixml/pugixml.hpp                     \
        pugixml/pugixml.cpp
else
libcalaos_common_la_SOURCES += \
        pugixml/pugiconfig.hpp                  \
        pugixml/pugixml.hpp                     \
        pugixml/pugixml.cpp
endif

EXTRA_DIST += pugixml/LICENSE.md

# T3.48 -- vendored third-party trees ship WHOLE, not file by file.
#
# Each directory below is an upstream import. Naming their files one by one is
# what left the release tarball unbuildable for 18 months: this Makefile.am
# shipped uvw/src/uvw.hpp and none of the 27 headers that umbrella includes,
# exprtk.hpp appeared in no variable at all, and 81 of uvw's 82 files, 36 of
# 36 in sqlite_modern_cpp and 31 of 31 in exprtk stayed behind -- along with
# the licences of llhttp, libquickmail, exprtk, sole and uvw, which makes the
# archive a redistribution problem as much as a broken build. A hand-written
# list also rots in silence at the next upstream import, so the directory, not
# the file, is the unit of distribution here: a `git subtree pull` that adds
# files needs no edit. tests/check-dist-coverage.sh fails if this list stops
# covering src/lib.
#
# Why not the shorter `EXTRA_DIST += uvw exprtk ...`: automake does copy a
# directory named in EXTRA_DIST recursively, but verbatim. An in-tree build
# leaves .deps/, .libs/ and *.lo inside llhttp/, libquickmail/, uri_parser/
# and pugixml/, so the tarball would depend on whether the source tree had
# been built -- the very kind of difference that hid this bug's size. The hook
# prunes them, and `make dist` yields the same archive from a fresh clone and
# from a fully built tree.
VENDORED_DIST_TREES = \
        cpptui                                  \
        exprtk                                  \
        libquickmail                            \
        llhttp                                  \
        pugixml                                 \
        sole                                    \
        sqlite_modern_cpp                       \
        uri_parser                              \
        uvw

dist-hook:
	@abs_srcdir=`cd $(srcdir) && pwd`; \
	abs_distdir=`cd $(distdir) && pwd`; \
	for tree in $(VENDORED_DIST_TREES); do \
	  test -d "$$abs_srcdir/$$tree" || \
	    { echo "dist-hook: no such vendored tree: $$tree" >&2; exit 1; }; \
	  (cd "$$abs_srcdir" && find "$$tree" \
	      \( -name .git -o -name .deps -o -name .libs \
	         -o -name __pycache__ \) -prune -o \
	      -type f ! -name '*.o' ! -name '*.lo' ! -name '*.la' \
	              ! -name '*.so' ! -name '*.gcno' ! -name '*.gcda' \
	              ! -name '.dirstamp' ! -name '*~' -print) \
	  | while read -r f; do \
	      $(MKDIR_P) "$$abs_distdir/$${f%/*}" || exit 1; \
	      cp -p "$$abs_srcdir/$$f" "$$abs_distdir/$$f" || exit 1; \
	    done || exit 1; \
	done
