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
