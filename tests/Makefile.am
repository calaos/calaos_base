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
              -I$(top_srcdir)/src/bin/tools/config_tui              \
              -I$(top_srcdir)/src/lib                               \
              -I$(top_srcdir)/src/lib/LuaScript                     \
              -I$(top_srcdir)/src/lib/TinyXML                       \
              -I$(top_srcdir)/src/lib/cJSON                         \
              -I$(top_srcdir)/src/lib/llhttp/include                \
              -I$(top_srcdir)/src/lib/libquickmail                  \
              -I$(top_srcdir)/src/lib/uri_parser                    \
              @CALAOS_COMMON_CFLAGS@
AM_LDFLAGS = -lgtest -lgtest_main

TESTS += ColorValue_test
check_PROGRAMS += ColorValue_test
ColorValue_test_SOURCES = ColorValue_test.cpp
ColorValue_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

TESTS += ExpressionEvaluator_test
check_PROGRAMS += ExpressionEvaluator_test
ExpressionEvaluator_test_SOURCES = ExpressionEvaluator_test.cpp
ExpressionEvaluator_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

TESTS += FileUtils_test
check_PROGRAMS += FileUtils_test
FileUtils_test_SOURCES = FileUtils_test.cpp
FileUtils_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

TESTS += ConfigOptions_test
check_PROGRAMS += ConfigOptions_test
ConfigOptions_test_SOURCES = ConfigOptions_test.cpp
ConfigOptions_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

TESTS += Utils_config_test
check_PROGRAMS += Utils_config_test
Utils_config_test_SOURCES = Utils_config_test.cpp
Utils_config_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

#ConfigModel lives with the TUI but does not include cpptui.hpp, so it is
#compiled straight into the test and runs without any terminal
TESTS += ConfigModel_test
check_PROGRAMS += ConfigModel_test
ConfigModel_test_SOURCES = ConfigModel_test.cpp \
                  $(top_srcdir)/src/bin/tools/config_tui/ConfigModel.cpp \
                  $(top_srcdir)/src/bin/tools/config_tui/ConfigModel.h
#Per target flags on purpose: they prefix the object name, so this build of
#ConfigModel.cpp cannot collide with the one of calaos_config
ConfigModel_test_CPPFLAGS = $(AM_CPPFLAGS)
ConfigModel_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la

endif

