#no test by default
TESTS =
check_PROGRAMS =

if HAVE_GTEST
AM_CPPFLAGS = \
              -I$(top_srcdir)/src \
              -DETC_DIR=\"$(sysconfdir)/calaos/\"                   \
              -DLIBMBUS                                             \
              -DPACKAGE_BIN_DIR=\"$(bindir)\"                       \
              -DPACKAGE_DATA_DIR=\"$(datadir)/calaos\"              \
              -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\"                  \
              -DPACKAGE_LIB_DIR=\"$(libdir)\"                       \
              -DTIXML_USE_STL                                       \
              -I$(top_srcdir)/src/bin/calaos_server/Audio           \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Gpio         \
              -I$(top_srcdir)/src/bin/calaos_server/IO/OneWire      \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Scripts      \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Wago/libmbus \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Wago         \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Web          \
              -I$(top_srcdir)/src/bin/calaos_server/IO/X10          \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Zibase       \
              -I$(top_srcdir)/src/bin/calaos_server/IO              \
              -I$(top_srcdir)/src/bin/calaos_server/IPCam           \
              -I$(top_srcdir)/src/bin/calaos_server/Rules           \
              -I$(top_srcdir)/src/bin/calaos_server/Scenario        \
              -I$(top_srcdir)/src/bin/calaos_server/TCPProcessor    \
              -I$(top_srcdir)/src/bin/calaos_server                 \
              -I$(top_srcdir)/src/bin/calaos_home                   \
              -I$(top_srcdir)/src/lib                               \
              -I$(top_srcdir)/src/lib/LuaScript                     \
              -I$(top_srcdir)/src/lib/TinyXML                       \
              -I$(top_srcdir)/src/lib/cJSON                         \
              -I$(top_srcdir)/src/lib/http-parser                   \
              -I$(top_srcdir)/src/lib/libquickmail                  \
              -I$(top_srcdir)/src/lib/uri_parser                    \
              @CALAOS_COMMON_CFLAGS@
AM_LDFLAGS = -lgtest -lgtest_main

TESTS += ColorValue_test
check_PROGRAMS += ColorValue_test
ColorValue_test_SOURCES = ColorValue_test.cpp
ColorValue_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

endif

if HAVE_AUTOBAHN
TESTS += WebSocketClient_test
check_PROGRAMS += WebSocketClient_test
WebSocketClient_test_SOURCES = WebSocketClient_test.cpp \
		$(top_builddir)/src/bin/calaos_home/WebSocketClient.cpp
WebSocketClient_test_LDADD = @CALAOS_SERVER_LIBS@ \
		$(top_builddir)/src/lib/libcalaos_common.la
endif
