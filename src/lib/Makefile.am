SUBDIRS = calaos-python

AM_CXXFLAGS = \
        $(CXX_PIC) \
        -DETC_DIR=\"$(sysconfdir)\" \
        -DPACKAGE_BIN_DIR=\"$(bindir)\" \
        -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\" \
        -DPACKAGE_LIB_DIR=\"$(libdir)\" \
        -DPACKAGE_LOCALE_DIR=\"$(datadir)/locale\" \
        -DTIXML_USE_STL \
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
        ExpressionEvaluator.h                   \
        ExpressionEvaluator.cpp                 \
        Timer.cpp                               \
        Timer.h                                 \
        Logger.cpp                              \
        Logger.h                                \
        Jansson_Addition.h                      \
        Json_Addition.h                         \
        NTPClock.cpp                            \
        NTPClock.h                              \
        Params.cpp                              \
        Params.h                                \
        Prefix.cpp                              \
        Prefix.h                                \
        SHA1.cpp                                \
        SHA1.h                                  \
        TimeRange.cpp                           \
        TimeRange.h                             \
        TinyXML/action_store.cpp                \
        TinyXML/action_store.h                  \
        TinyXML/byte_stream.h                   \
        TinyXML/lex_token.h                     \
        TinyXML/lex_util.cpp                    \
        TinyXML/lex_util.h                      \
        TinyXML/node_set.cpp                    \
        TinyXML/node_set.h                      \
        TinyXML/tinystr.cpp                     \
        TinyXML/tinystr.h                       \
        TinyXML/tinystr.h                       \
        TinyXML/tinyxml.cpp                     \
        TinyXML/tinyxml.h                       \
        TinyXML/tinyxml.h                       \
        TinyXML/tinyxmlerror.cpp                \
        TinyXML/tinyxmlparser.cpp               \
        TinyXML/tinyxpath_conf.h                \
        TinyXML/tokenlist.cpp                   \
        TinyXML/tokenlist.h                     \
        TinyXML/xml_util.cpp                    \
        TinyXML/xml_util.h                      \
        TinyXML/xpath_expression.cpp            \
        TinyXML/xpath_expression.h              \
        TinyXML/xpath_processor.cpp             \
        TinyXML/xpath_processor.h               \
        TinyXML/xpath_stack.cpp                 \
        TinyXML/xpath_stack.h                   \
        TinyXML/xpath_static.cpp                \
        TinyXML/xpath_static.h                  \
        TinyXML/xpath_stream.cpp                \
        TinyXML/xpath_stream.h                  \
        TinyXML/xpath_syntax.cpp                \
        TinyXML/xpath_syntax.h                  \
        UrlDownloader.cpp                       \
        UrlDownloader.h                         \
        Utils.cpp                               \
        Utils.h                                 \
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

MAINTAINERCLEANFILES = Makefile.in
