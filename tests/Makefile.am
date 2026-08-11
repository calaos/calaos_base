#no test by default
TESTS =
check_PROGRAMS =

#Shell tests. They need no gtest, only the built calaos_config, so they live
#outside of the HAVE_GTEST conditional. They locate the tree through those two
#variables, and fall back on their own path when run by hand.
AM_TESTS_ENVIRONMENT = \
              abs_top_srcdir='$(abs_top_srcdir)'; export abs_top_srcdir;     \
              abs_top_builddir='$(abs_top_builddir)'; export abs_top_builddir;

#check-config-options.sh: every config key used by the sources must be in the
#ConfigOptions registry, no registry entry may be orphaned, no obsolete key may
#come back.
#check-config-docs.sh: docs/16_config_options.md must still match
#"calaos_config options --markdown".
TESTS += check-config-options.sh check-config-docs.sh
dist_check_SCRIPTS = check-config-options.sh check-config-docs.sh

if HAVE_GTEST
AM_CPPFLAGS = \
              -I$(top_srcdir)/src \
              -DETC_DIR=\"$(sysconfdir)/calaos/\"                   \
              -DLIBMBUS                                             \
              -DPACKAGE_BIN_DIR=\"$(bindir)\"                       \
              -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\"                  \
              -DPACKAGE_LIB_DIR=\"$(libdir)\"                       \
              -DTIXML_USE_STL                                       \
              -I$(top_srcdir)/src/bin/calaos_server/Audio           \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Gpio         \
              -I$(top_srcdir)/src/bin/calaos_server/IO/OneWire      \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Wago/libmbus \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Wago         \
              -I$(top_srcdir)/src/bin/calaos_server/IO/Web          \
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
              -I$(top_srcdir)/src/lib/llhttp/include                \
              -I$(top_srcdir)/src/lib/libquickmail                  \
              -I$(top_srcdir)/src/lib/uri_parser                    \
              @CALAOS_COMMON_CFLAGS@

TESTS += ColorValue_test
check_PROGRAMS += ColorValue_test
ColorValue_test_SOURCES = ColorValue_test.cpp
ColorValue_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += ExpressionEvaluator_test
check_PROGRAMS += ExpressionEvaluator_test
ExpressionEvaluator_test_SOURCES = ExpressionEvaluator_test.cpp
ExpressionEvaluator_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += FileUtils_test
check_PROGRAMS += FileUtils_test
FileUtils_test_SOURCES = FileUtils_test.cpp
FileUtils_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += ConfigOptions_test
check_PROGRAMS += ConfigOptions_test
ConfigOptions_test_SOURCES = ConfigOptions_test.cpp
ConfigOptions_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += Utils_config_test
check_PROGRAMS += Utils_config_test
Utils_config_test_SOURCES = Utils_config_test.cpp
Utils_config_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

#Core tests (tests/core/). They exercise the server core itself: Config,
#IOFactory, IOBase, Room, ListeRoom, Rule, ListeRule, Condition* and Action*.
#
#Linking: calaos_server is a program, not a library, so there is no .la to link
#against. Its objects are reused as they are. That was preferred over the two
#alternatives because it needs no change at all outside of this file (the
#ticket owns tests/ only) and it does not compile the same 30 sources a second
#time. It also keeps the IOFactory self registrations working: REGISTER_IO()
#creates a static object in the .cpp of each IO, so an IO type only exists in
#a test binary when its object file is listed here. The list below is exactly
#the transitive closure needed by CalaosConfig.o, and it deliberately contains
#a single family of IOs, the internal ones (IO/IntValue.o gives InternalBool,
#InternalInt and InternalString): no hardware driver, no subprocess.
#
#src/ is built before tests/ (top level SUBDIRS = src data tests) so these
#objects always exist by the time a test is linked. They are not repeated in
#_DEPENDENCIES on purpose: they are built by another Makefile and make has no
#rule for them here.
CALAOS_SERVER_BUILDDIR = $(top_builddir)/src/bin/calaos_server

CORE_SERVER_OBJECTS = \
                  $(CALAOS_SERVER_BUILDDIR)/Calaos.$(OBJEXT)                     \
                  $(CALAOS_SERVER_BUILDDIR)/CalaosConfig.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/DataLogger.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/EventManager.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/HistLogger.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/IOBase.$(OBJEXT)                     \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApi.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/ListeRoom.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/ListeRule.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/NotifManager.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/Room.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/Rule.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioPlayer.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/IPCam.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/IO/IODoc.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/IOFactory.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InPlageHoraire.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputTimer.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/IO/IntValue.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Scenario.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptExec.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/Action.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ActionMail.$(OBJEXT)           \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ActionPush.$(OBJEXT)           \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ActionScript.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ActionStd.$(OBJEXT)            \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ActionTouchscreen.$(OBJEXT)    \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/Condition.$(OBJEXT)            \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ConditionOutput.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ConditionScript.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ConditionStart.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/ConditionStd.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Rules/RulesFactory.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Scenario/AutoScenario.$(OBJEXT)

#The server headers pulled in by the core fixture need the libuv/curl/openssl
#include paths, which the common flags above do not carry.
CORE_TEST_CPPFLAGS = $(AM_CPPFLAGS)                    \
                  -I$(top_srcdir)/src/bin/calaos_server/LuaScript \
                  -I$(top_srcdir)/src/lib/sole                    \
                  -I$(top_srcdir)/src/lib/sqlite_modern_cpp/hdr   \
                  @CALAOS_SERVER_CFLAGS@

CORE_TEST_LDADD = $(CORE_SERVER_OBJECTS) \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += core/CoreSmoke_test
check_PROGRAMS += core/CoreSmoke_test
core_CoreSmoke_test_SOURCES = core/CoreSmoke_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_CoreSmoke_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_CoreSmoke_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_CoreSmoke_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la

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
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

# T1.12
TESTS += Utils_rand_test
check_PROGRAMS += Utils_rand_test
Utils_rand_test_SOURCES = Utils_rand_test.cpp
Utils_rand_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += TcpSocket_test
check_PROGRAMS += TcpSocket_test
TcpSocket_test_SOURCES = TcpSocket_test.cpp
#Needs std::thread (accept() runs on a background thread while the test
#connects from the main thread over loopback).
TcpSocket_test_CPPFLAGS = $(AM_CPPFLAGS) @PTHREAD_CFLAGS@
TcpSocket_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main \
                  @PTHREAD_LIBS@ -lpthread
# T1.13
#LAN (PingInputSwitch, WOLOutputBool) and Hue (HueOutputLightRGB) IOs. Same
#core objects as the smoke test, plus the four object files of those IOs and
#of their two base classes. Those are the only hardware IOs linked into a
#test: they need nothing more than libuv (the ping subprocess is spawned by
#the IO itself, and no libuv loop runs here), no curl/mbus/lua extra.
TESTS += core/LanHue_test
check_PROGRAMS += core/LanHue_test
core_LanHue_test_SOURCES = core/LanHue_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_LanHue_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/LAN \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Hue
core_LanHue_test_LDADD = $(CORE_TEST_LDADD) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitch.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightRGB.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/LAN/PingInputSwitch.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/LAN/WOLOutputBool.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Hue/HueOutputLightRGB.$(OBJEXT)
#Built elsewhere, do not let automake turn them into prerequisites
core_LanHue_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la

endif


# T1.1
#Rule/IO lifecycle tests. Own HAVE_GTEST block so that this stays a pure
#append at the end of the file (several tickets add their block here).
if HAVE_GTEST
TESTS += core/RuleLifecycle_test
check_PROGRAMS += core/RuleLifecycle_test
core_RuleLifecycle_test_SOURCES = core/RuleLifecycle_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_RuleLifecycle_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_RuleLifecycle_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RuleLifecycle_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif


# T1.15
#Lua sandbox and execution watchdog. ScriptManager.cpp/ScriptBindings.cpp are
#only built into calaos_script (a program, like calaos_server), so their objects
#are reused directly the same way the core tests reuse the server ones. Own
#HAVE_GTEST block so that this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += LuaSandbox_test
check_PROGRAMS += LuaSandbox_test
LuaSandbox_test_SOURCES = LuaSandbox_test.cpp
LuaSandbox_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
LuaSandbox_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptManager.$(OBJEXT)   \
                  $(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptBindings.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT)             \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
LuaSandbox_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T1.3
#ListeRoom robustness tests: default name, EventIOAdded only on success, and
#duplicate id handling in io_table. Own HAVE_GTEST block, same reasoning as
#T1.1: keeps this a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/ListeRoomRobustness_test
check_PROGRAMS += core/ListeRoomRobustness_test
core_ListeRoomRobustness_test_SOURCES = core/ListeRoomRobustness_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ListeRoomRobustness_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ListeRoomRobustness_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ListeRoomRobustness_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T1.2
#ConditionStd/ConditionOutput semantics: multi-input conjunction and
#ConditionStd/ConditionOutput eval() parity. Own HAVE_GTEST block, same
#reason as T1.1's above.
if HAVE_GTEST
TESTS += core/ConditionEval_test
check_PROGRAMS += core/ConditionEval_test
core_ConditionEval_test_SOURCES = core/ConditionEval_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ConditionEval_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ConditionEval_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ConditionEval_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
# T1.4
#Hardening of the json api: security helpers and login throttle. Own HAVE_GTEST
#block so that this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiHardening_test
check_PROGRAMS += core/JsonApiHardening_test
core_JsonApiHardening_test_SOURCES = core/JsonApiHardening_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiHardening_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_JsonApiHardening_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_JsonApiHardening_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
# T1.5
#WebSocket frame parsing: 64 bits payload length and frame size cap.
#WebSocketFrame lives in src/lib and parses a byte buffer, so it needs
#nothing more than libcalaos_common (no event loop, no server object).
if HAVE_GTEST
TESTS += WebSocketFrame_test
check_PROGRAMS += WebSocketFrame_test
WebSocketFrame_test_SOURCES = WebSocketFrame_test.cpp
WebSocketFrame_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T1.9
#ExternProc framing parser: 5 byte header guard (no OOB read on short
#buffers), big endian length decoding, payload length cap with recovery.
#ExternProcMessage is built into the calaos_server program, so its object is
#reused directly, the same way LuaSandbox_test does above. Own HAVE_GTEST
#block so that this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += ExternProcFraming_test
check_PROGRAMS += ExternProcFraming_test
ExternProcFraming_test_SOURCES = ExternProcFraming_test.cpp
ExternProcFraming_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
ExternProcFraming_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT) \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
ExternProcFraming_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
# T1.6
#RemoteUI HMAC constant-time comparison and dedup'd auth helpers. The helpers
#(constantTimeHexEquals, hexDecode, findHeader) are header-inline in
#HMACAuthenticator.h, so only openssl (via @CALAOS_SERVER_LIBS@) is needed at
#link time, no server objects. CORE_TEST_CPPFLAGS brings the libuv/openssl
#include paths pulled transitively by the RemoteUI headers. Own HAVE_GTEST
#block so that this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += RemoteUIHmac_test
check_PROGRAMS += RemoteUIHmac_test
RemoteUIHmac_test_SOURCES = RemoteUIHmac_test.cpp
RemoteUIHmac_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI
RemoteUIHmac_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T1.11
#IOBase/IOFactory id integrity: "id" immutable via set_param/del_param,
#REGISTER_IO collision keeps the first registration, genDoc does not pollute
#the live io_table, battery status contract. Own HAVE_GTEST block so that
#this stays a pure append at the end of the file (same reasoning as T1.1).
if HAVE_GTEST
TESTS += core/IOIdIntegrity_test
check_PROGRAMS += core/IOIdIntegrity_test
core_IOIdIntegrity_test_SOURCES = core/IOIdIntegrity_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_IOIdIntegrity_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_IOIdIntegrity_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_IOIdIntegrity_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
