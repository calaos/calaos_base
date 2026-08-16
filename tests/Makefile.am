#no test by default
TESTS =
check_PROGRAMS =

#Shell tests. They need no gtest, only the built calaos_config, so they live
#outside of the HAVE_GTEST conditional. They locate the tree through those two
#variables, and fall back on their own path when run by hand.
AM_TESTS_ENVIRONMENT = \
              abs_top_srcdir='$(abs_top_srcdir)'; export abs_top_srcdir;     \
              abs_top_builddir='$(abs_top_builddir)'; export abs_top_builddir; \
              PYTHON='$(PYTHON)'; export PYTHON;

#check-config-options.sh: every config key used by the sources must be in the
#ConfigOptions registry, no registry entry may be orphaned, no obsolete key may
#come back.
#check-config-docs.sh: docs/16_config_options.md must still match
#"calaos_config options --markdown".
TESTS += check-config-options.sh check-config-docs.sh
dist_check_SCRIPTS = check-config-options.sh check-config-docs.sh

# T2.14: python suites (tests/python/, from T1.8 + T1.16) wired into make
# check through a wrapper. It runs pytest when available, falls back to the
# stdlib "unittest discover" for the test_t116_*.py suites, and exits 77
# (automake SKIP) when no usable python3 exists. PYTHON comes from
# AM_PATH_PYTHON via AM_TESTS_ENVIRONMENT above.
TESTS += run-python-tests.sh
dist_check_SCRIPTS += run-python-tests.sh
EXTRA_DIST = \
              python/conftest.py             \
              python/test_auth.py            \
              python/test_extern_proc.py     \
              python/test_logger.py          \
              python/test_t116_mcp_client.py \
              python/test_t116_mcp_config_io.py \
              python/test_t116_roon.py

if HAVE_GTEST
AM_CPPFLAGS = \
              -I$(top_srcdir)/src \
              -DETC_DIR=\"$(sysconfdir)/calaos/\"                   \
              -DLIBMBUS                                             \
              -DPACKAGE_BIN_DIR=\"$(bindir)\"                       \
              -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\"                  \
              -DPACKAGE_LIB_DIR=\"$(libdir)\"                       \
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

# T1.10
#RemoteUI WebSocket/OTA lifecycle helpers: non-throwing grid dimension
#parsing (parseGridDimension) and 64-bit clamped OTA rescan interval
#(computeRescanIntervalMs). Both helpers are header-inline in the RemoteUI
#headers, so only the libs are needed at link time, no server objects (same
#setup as RemoteUIHmac_test above). Own HAVE_GTEST block so that this stays
#a pure append at the end of the file.
if HAVE_GTEST
TESTS += RemoteUIOtaLifecycle_test
check_PROGRAMS += RemoteUIOtaLifecycle_test
RemoteUIOtaLifecycle_test_SOURCES = RemoteUIOtaLifecycle_test.cpp
RemoteUIOtaLifecycle_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI
RemoteUIOtaLifecycle_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T1.19 (trimmed by T2.12)
# IO controller regression: WebCtrl::getValueText empty-path guard.
# Links WebCtrl from the server build plus the core closure. Own
# HAVE_GTEST block so that this stays a pure append at the end of the
# file (same reasoning as T1.1).
if HAVE_GTEST
TESTS += IOControllers_test
check_PROGRAMS += IOControllers_test
IOControllers_test_SOURCES = IOControllers_test.cpp
IOControllers_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
IOControllers_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebCtrl.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
IOControllers_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
# T1.14
#Reolink event registry: register/unregister bookkeeping + reentrant-safe
#dispatch (UAF regression: ReolinkInputSwitch callbacks stored in the
#ReolinkCtrl singleton had no unregister path). The registry is a pure-std
#header (no process, no event loop), listed in _SOURCES so it also lands in
#the dist tarball. Own HAVE_GTEST block so this stays a pure append at the
#end of the file.
if HAVE_GTEST
TESTS += ReolinkRegistry_test
check_PROGRAMS += ReolinkRegistry_test
ReolinkRegistry_test_SOURCES = ReolinkRegistry_test.cpp \
                  $(top_srcdir)/src/bin/calaos_server/IO/Reolink/ReolinkEventRegistry.h
ReolinkRegistry_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Reolink
ReolinkRegistry_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T1.17
#Extern-proc driver mains: OWFS scan filter boundary-hex regression
#(OWFSUtils.h) and Wago subprocess respawn backoff bounds (WagoMap.h static
#helper). Header-only logic under test, so no driver objects are linked.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += ExternProcDrivers_test
check_PROGRAMS += ExternProcDrivers_test
ExternProcDrivers_test_SOURCES = ExternProcDrivers_test.cpp
ExternProcDrivers_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
ExternProcDrivers_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T2.3
#WebSocket Sec-WebSocket-Accept regression after swapping the bundled CSHA1
#for OpenSSL EVP_Digest in WebSocket.cpp: RFC 6455 known-answer vector
#(base64(SHA1(key+GUID))) via the same EVP + Utils::Base64_encode pipeline.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += WebSocketAccept_test
check_PROGRAMS += WebSocketAccept_test
WebSocketAccept_test_SOURCES = WebSocketAccept_test.cpp
WebSocketAccept_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
WebSocketAccept_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T2.6
#Common-lib correctness regressions: ThreadedQueue::tryPop drains the queue,
#ColorValue::setString rejects malformed strings without throwing, Calendar
#month/day roll-over (monthDown Jan->Dec, dayDown commits the day), and
#Params::Parse is safe on high (non-ASCII) bytes. Everything under test lives
#in libcalaos_common, so nothing more is linked. Own HAVE_GTEST block so this
#stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += CommonLib_test
check_PROGRAMS += CommonLib_test
CommonLib_test_SOURCES = CommonLib_test.cpp
CommonLib_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T2.1
#Timer lifetime: singleShot rewritten as a true one-shot with deferred
#handle destruction, weak alive-tag making delete-from-inside-the-callback
#and destruction-with-pending-callback safe. Pure src/lib code, so only
#libcalaos_common is linked and the test runs the default uvw loop itself
#(every wait is wall-clock bounded, no fixture needed). Own HAVE_GTEST
#block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/Timer_test
check_PROGRAMS += core/Timer_test
core_Timer_test_SOURCES = core/Timer_test.cpp
core_Timer_test_CPPFLAGS = $(AM_CPPFLAGS)
core_Timer_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T2.10
#UrlDownloader lifecycle: cancel() really interrupts the transfer, no callback
#fires after cancel/destroy (alive-token), autodelete objects free themselves
#safely on cancel, the accumulated response body is bounded, and no temp
#file/fd leaks across lifecycles. Behavioral only (local HTTP peer over
#loopback, no subprocess assertions) so the planned libcurl backend swap
#(T2.5) must keep it green. Own HAVE_GTEST block so this stays a pure append
#at the end of the file.
if HAVE_GTEST
TESTS += UrlDownloader_test
check_PROGRAMS += UrlDownloader_test
UrlDownloader_test_SOURCES = UrlDownloader_test.cpp
UrlDownloader_test_CPPFLAGS = $(AM_CPPFLAGS)
UrlDownloader_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T2.4
#CalaosConfig robustness: corrupt io.xml/rules.xml no longer exit(-1) the
#daemon (backup restore fallback), atomic tmp+rename saves, state cache
#deserialization guarded against malformed JSON, and the ~Config() shutdown
#flush of pending states. Reuses the T0.3 core fixture (config/cache dirs,
#singleton cleanup) and the CORE_SERVER_OBJECTS closure that already carries
#CalaosConfig.o. Own HAVE_GTEST block so this stays a pure append at the end
#of the file.
if HAVE_GTEST
TESTS += core/CalaosConfigRobustness_test
check_PROGRAMS += core/CalaosConfigRobustness_test
core_CalaosConfigRobustness_test_SOURCES = core/CalaosConfigRobustness_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_CalaosConfigRobustness_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_CalaosConfigRobustness_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_CalaosConfigRobustness_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T2.11
#Transport hardening: WebSocketFrame condemns an invalid header (oversized or
#fragmented control frame, RSV bits) at the header without buffering the
#payload of a refused frame; TransportLimits::parseLimit clamps broken config
#overrides back to the built-in defaults; effectiveClientIp identifies a
#client by the last entry of the last X-Forwarded-For line (trusted haproxy
#hop, T1.8 rule) with TCP-peer fallback. Only libcalaos_common code and
#header-inline helpers of HttpClient.h are exercised, so nothing of the
#calaos_server binary is linked. Own HAVE_GTEST block so this stays a pure
#append at the end of the file.
if HAVE_GTEST
TESTS += TransportHardening_test
check_PROGRAMS += TransportHardening_test
TransportHardening_test_SOURCES = TransportHardening_test.cpp
TransportHardening_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.4
#base64 hardening: the vendored decoder (base64.cpp) now strictly validates
#RFC 4648 input — out-of-alphabet chars (whitespace, NUL, high bytes),
#misplaced/broken '=' padding, impossible 4k+1 lengths and non-canonical
#trailing bits are rejected (std::nullopt via base64_decode_checked) instead
#of silently truncating; the legacy base64_decode()/Utils::Base64_* entry
#points keep their signatures and map rejection to "". Also covers the
#Utils::Base64_encode(void*, int) negative-size OOB guard and RFC 4648 §10
#round-trip vectors. Only libcalaos_common code is exercised. Own HAVE_GTEST
#block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += Base64_test
check_PROGRAMS += Base64_test
Base64_test_SOURCES = Base64_test.cpp
Base64_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.3
#IPCam cleanup + Synology snapshot fix. IPCam::maskUrlCredentials (defined in
#IPCam.cpp, whose object is already part of CORE_SERVER_OBJECTS) masks camera
#credentials before URLs reach the logs; SynoSurveillanceStation::
#snapshotPayload is a header-inline pure helper guaranteeing the snapshot
#completion callback fires exactly once (the old inline check was missing a
#return and fired it twice on an error/JSON body). Own HAVE_GTEST block so
#this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += IPCamUrl_test
check_PROGRAMS += IPCamUrl_test
IPCamUrl_test_SOURCES = IPCamUrl_test.cpp
IPCamUrl_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
IPCamUrl_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
IPCamUrl_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.5
#Squeezebox protocol helpers: CLI chunk reassembly (the buffered-fragment
#path used to discard the chunk carrying the terminating newline, and an
#all-newline chunk read msg[-1] in the trailing trim) and cover-art URL
#building (the ?playerid= current-song fallback branch was unreachable).
#Both helpers are static header-inline in Squeezebox.h, so nothing of the
#calaos_server binary is linked. Own HAVE_GTEST block so this stays a pure
#append at the end of the file.
if HAVE_GTEST
TESTS += Squeezebox_test
check_PROGRAMS += Squeezebox_test
Squeezebox_test_SOURCES = Squeezebox_test.cpp
Squeezebox_test_CPPFLAGS = $(AM_CPPFLAGS)
Squeezebox_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.2e
#Wago thin IO subclasses: the WIDigitalBP/Long/Triple and WOVolet(Smart)
#skeletons are factored into WIDigitalBase<>/WOVoletBase<> (WagoIOBase.h);
#the numeric config values (modbus address, port) now go through
#WagoConfigParse, a bounded non-throwing parse following the
#GpioCtrl::parseDebounceTime pattern (T2.13). Only the header-inline
#WagoConfigParse helpers are exercised here (std + libcalaos_common),
#nothing of the calaos_server binary is linked. Own HAVE_GTEST block so
#this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += WagoConfigParse_test
check_PROGRAMS += WagoConfigParse_test
WagoConfigParse_test_SOURCES = WagoConfigParse_test.cpp
WagoConfigParse_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.2d
#Thin Web IO subclasses: the seven Web types (WebInput{Analog,String,Temp},
#WebOutput{Analog,Light,LightRGB,String}) share their constructor skeleton
#through the WebIOBase/WebInputBase mixins of WebDocBase.h. The test pins
#what must not change: IOFactory registration under the XML type names,
#instantiation from an io.xml document, the per-type WebDocBase ioDoc
#parameters (--gendoc content), the synchronous first read from a local
#file:// url and the create/delete cycle. Links the Web IO objects plus
#their base classes on top of the core closure. Own HAVE_GTEST block so
#this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/WebIO_test
check_PROGRAMS += core/WebIO_test
core_WebIO_test_SOURCES = core/WebIO_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_WebIO_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_WebIO_test_LDADD = $(CORE_TEST_LDADD) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/AnalogIO.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputString.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputTemp.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightRGB.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputString.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebDocBase.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebInputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebInputString.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebInputTemp.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebOutputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebOutputLight.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebOutputLightRGB.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebOutputString.$(OBJEXT)
#Built elsewhere, do not let automake turn them into prerequisites
core_WebIO_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
# T3.2c
#Mqtt thin IO subclasses: MqttOutputLightRGB color+state feedback fix. The
#on/off + last-known-color decision is the header-inline static
#MqttOutputLightRGB::stateFromColor() (pure ColorValue logic), so no Mqtt
#object nor extern process is linked, only libcalaos_common for ColorValue
#(same approach as ExternProcDrivers_test). Own HAVE_GTEST block so this
#stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += MqttIO_test
check_PROGRAMS += MqttIO_test
MqttIO_test_SOURCES = MqttIO_test.cpp
MqttIO_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Mqtt
MqttIO_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.1
#AVReceiver correctness: Onkyo eISCP reassembly no longer inserts with an
#iterator of another vector (UB), zone-2/3 input-source and Pioneer zone-3
#volume land on their own signals, Onkyo hex volumes with letters are parsed,
#Denon/Marantz/Onkyo setVolume commands are really zero-padded, and
#AVRManager::Delete(NULL) is a no-op. The AVR driver objects of calaos_server
#are reused as they are (same approach and reasoning as CORE_SERVER_OBJECTS),
#no loop is ever run and sendRequest() is overridden, so no network I/O.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
AVR_TEST_OBJECTS = \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVReceiver.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRManager.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRDenon.$(OBJEXT)           \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRMarantz.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVROnkyo.$(OBJEXT)           \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRPioneer.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRYamaha.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRRose.$(OBJEXT)            \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AVRRoseNotifServer.$(OBJEXT)

TESTS += AVReceiver_test
check_PROGRAMS += AVReceiver_test
AVReceiver_test_SOURCES = AVReceiver_test.cpp
AVReceiver_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
AVReceiver_test_LDADD = $(AVR_TEST_OBJECTS) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
AVReceiver_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.2a
#Thin KNX IO subclasses collapsed into KNXIo<Base> (IO/KNX/KNXIo.cpp). The
#eleven registered XML type names are the ABI to existing io.xml configs:
#each must stay registered in IOFactory and instanciable through the real
#ListeRoom::createIO() path. Same linking approach as core/LanHue_test: the
#core objects plus the KNX objects and the base classes of the eleven thin
#subclasses. No libuv loop runs here; the calaos_knx helper spawned by
#KNXCtrl does not exist next to the test binary, so uv_spawn fails cleanly.
if HAVE_GTEST
TESTS += core/KnxIo_test
check_PROGRAMS += core/KnxIo_test
core_KnxIo_test_SOURCES = core/KnxIo_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_KnxIo_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/KNX
core_KnxIo_test_LDADD = $(CORE_TEST_LDADD) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/AnalogIO.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitch.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitchLongPress.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitchTriple.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputTemp.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputAnalog.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightDimmer.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightRGB.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutter.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutterSmart.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXBase.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXIo.$(OBJEXT)
core_KnxIo_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T2.15
#Async audio-player chain of JsonApi::buildJsonState(): destruction of the
#JsonApi (client disconnect) or of the AudioPlayer IO while squeezebox answers
#are in flight must not be a use-after-free, the answer must still go out when
#a player dies mid-chain, and the per-player json object must not leak a
#reference (json_object_set vs _new). A FakeAudioPlayer subclass queues the
#callbacks so the tests deliver every answer by hand: no loop, no network.
#Links the plain core objects only (JsonApi.o and Audio/AudioPlayer.o are
#already in CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a pure
#append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiAudioState_test
check_PROGRAMS += core/JsonApiAudioState_test
core_JsonApiAudioState_test_SOURCES = core/JsonApiAudioState_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiAudioState_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_JsonApiAudioState_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_JsonApiAudioState_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T2.19
#TLS insecure by DEFAULT for user-configured URLs (user decision, supersedes
#the T2.17 default on those paths): UrlDownloader::insecureParamEnabled maps
#a device `insecure` param to the effective policy (absent/empty/anything ->
#insecure, exact "false" -> verified) and setInsecureFromParam applies it.
#Pins the grandfathering contract (configs without any insecure param behave
#exactly like pre-T2.17) and that a plain downloader stays verified for the
#hardcoded calaos.fr services. Only libcalaos_common code is exercised. Own
#HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += TlsInsecureDefault_test
check_PROGRAMS += TlsInsecureDefault_test
TlsInsecureDefault_test_SOURCES = TlsInsecureDefault_test.cpp
TlsInsecureDefault_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T2.18
#Null-guard audit of the createIO()/IOFactory::CreateIO() call sites: a
#factory miss (unknown type, squatted internal id, missing room) must end in
#a logged error and an {"error"} API answer, never in a null dereference.
#Links the plain core objects only (JsonApi.o, Scenario.o and AutoScenario.o
#are already in CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a
#pure append at the end of the file.
if HAVE_GTEST
TESTS += core/ScenarioNullGuard_test
check_PROGRAMS += core/ScenarioNullGuard_test
core_ScenarioNullGuard_test_SOURCES = core/ScenarioNullGuard_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ScenarioNullGuard_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ScenarioNullGuard_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ScenarioNullGuard_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.2a
#By-id resolution accessors of ListeRoom (findIO/hasIO/findIOAs<T>/
#findIOByIndex/findRoomOfIO), first step of the E4.2 ownership series. Pins the
#accessor contract (unknown id, empty id, duplicate id, out of range -> nullptr,
#and a miss never inserts into io_table) plus the three invariants the next
#steps must not break: the room-by-room IO iteration order, the
#getCameraList()/getAudioList() signatures and contents, and the
#delete_io(io, del) ownership-transfer semantics.
#Links the plain core objects only (ListeRoom.o is already in
#CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a pure append at the
#end of the file.
if HAVE_GTEST
TESTS += core/ListeRoomIdResolution_test
check_PROGRAMS += core/ListeRoomIdResolution_test
core_ListeRoomIdResolution_test_SOURCES = core/ListeRoomIdResolution_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ListeRoomIdResolution_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ListeRoomIdResolution_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ListeRoomIdResolution_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.4a
#pugixml availability smoke test. Build contract, not calaos behaviour: it
#proves the header is reachable, the library links, and -- the point of the
#whole E4.4 series -- that XPath 1.0 is compiled in. select_node()/
#select_nodes()/xpath_query do not exist in a PUGIXML_NO_XPATH build, so
#PugiXml_test.cpp cannot compile against one.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += PugiXml_test
check_PROGRAMS += PugiXml_test
PugiXml_test_SOURCES = PugiXml_test.cpp
PugiXml_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# E4.3a
#First tests for src/lib/TimeRange.h and for the evaluation of
#IO/InPlageHoraire.cpp (both had zero test reference). Everything is asserted
#on observable behaviour - the schedules are built through the public
#AddMonday()/... API and read back through get_value_bool() - so the suite
#survives the tinyxml -> pugixml port of InPlageHoraire (E4.4d) untouched.
#Also covers the Calendar corners tests/CommonLib_test.cpp does not (leap year,
#day clamping, hour/minute/second wrap).
#Lives in tests/ but links like a core test: InPlageHoraire needs IOBase,
#ListeRoom, ListeRule and the config singleton. InPlageHoraire.o is already
#part of CORE_SERVER_OBJECTS. Own HAVE_GTEST block so this stays a pure append
#at the end of the file.
if HAVE_GTEST
TESTS += TimeRangeCalendar_test
check_PROGRAMS += TimeRangeCalendar_test
TimeRangeCalendar_test_SOURCES = TimeRangeCalendar_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
TimeRangeCalendar_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
TimeRangeCalendar_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
TimeRangeCalendar_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.3b
#The config round trip referee: config -> save -> reload -> the in-memory model
#must be identical (rooms, IO ids, IO parameters, InPlageHoraire schedules,
#rules with their inputs/operators/values), with accented UTF-8, XML entities,
#numeric character references and empty values in the payload. The comparison
#is SEMANTIC on purpose, never byte for byte: E4.4d (pugixml) will legitimately
#reformat io.xml/rules.xml and this suite is what says the content survived.
#Links the plain core objects only (InPlageHoraire.o is already in
#CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a pure append at the
#end of the file.
if HAVE_GTEST
TESTS += core/ConfigRoundTrip_test
check_PROGRAMS += core/ConfigRoundTrip_test
core_ConfigRoundTrip_test_SOURCES = core/ConfigRoundTrip_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ConfigRoundTrip_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ConfigRoundTrip_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ConfigRoundTrip_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.2b
#Explicit and unique ownership of the IOs (step 2/6 of the E4.2 series): a Room
#owns its IOs through unique_ptr, ListeRoom's io_table/cameraCache/audioCache
#are non-owning indexes, and there is no manual delete left on the IO path.
#Pins the two ownership TRANSFERS out of a room (delete_io(io, del=false) and
#RemoveIOFromRoom(), which must be release() and not a destructive erase), that
#removing an IO twice is a no-op rather than a double free, and that destroying
#a Room - registered in ListeRoom or not - destroys every IO it owns and leaks
#none. Also covers the E4.2a createIO() carry-over (no half-added IO).
#Links the plain core objects only (ListeRoom.o and Room.o are already in
#CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a pure append at the
#end of the file.
if HAVE_GTEST
TESTS += core/ListeRoomOwnership_test
check_PROGRAMS += core/ListeRoomOwnership_test
core_ListeRoomOwnership_test_SOURCES = core/ListeRoomOwnership_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ListeRoomOwnership_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ListeRoomOwnership_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ListeRoomOwnership_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.4b
#WebCtrl::getValueXml() after the TinyXPath -> pugixml XPath 1.0 port. This is
#the only untrusted XML parse in calaos (the document comes from a
#user-configured URL) and the only site where the XPath expression is itself
#user config, so the suite pins both halves: the expression shapes real configs
#use keep returning what TinyXPath returned, and hostile/malformed input plus an
#invalid expression fail clean (empty string, no escaped xpath_exception, no
#abort). Links WebCtrl.o from the server build plus the core closure, exactly
#like IOControllers_test above. Own HAVE_GTEST block so this stays a pure append
#at the end of the file.
if HAVE_GTEST
TESTS += WebCtrlXPath_test
check_PROGRAMS += WebCtrlXPath_test
WebCtrlXPath_test_SOURCES = WebCtrlXPath_test.cpp
WebCtrlXPath_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
WebCtrlXPath_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebCtrl.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
WebCtrlXPath_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

#Conditions/Actions reference their IOs BY ID (step 3/6 of the E4.2 series):
#nothing in Rules/ stores an IOBase* any more, everything resolves through the
#E4.2a accessors at the point of use. Pins the model (a reference follows the
#id, not the object), the missing-IO contract (condition false, action skipped,
#both logged, save keeps the id, load-time rejection unchanged), the in_event
#unregistration now driven by registration instead of a gui_type whitelist, the
#dispatch order, and prints the cost of the model on a 200-rule config.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/RuleIoReference_test
check_PROGRAMS += core/RuleIoReference_test
core_RuleIoReference_test_SOURCES = core/RuleIoReference_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_RuleIoReference_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_RuleIoReference_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RuleIoReference_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.15
#RemoteUI <calaos:device_info> round trip: the element used to be written on
#the ROOM node and read back inside <calaos:remote_ui>, so it never made it
#home. Pins the round trip (save -> reload -> identical), the placement in the
#file, the one shot migration of the orphans the broken writer left under the
#room, the fact that a device cannot adopt the orphan of its neighbour, and
#that an empty device_info still writes nothing.
#Links the transitive closure of IO/RemoteUI/RemoteUI.o on top of the core
#objects: RemoteUI calls RemoteUIManager, which drags the WebSocket/OTA/HTTP
#side of the server with it. Own HAVE_GTEST block so this stays a pure append
#at the end of the file.
if HAVE_GTEST
TESTS += core/RemoteUIDeviceInfo_test
check_PROGRAMS += core/RemoteUIDeviceInfo_test
core_RemoteUIDeviceInfo_test_SOURCES = core/RemoteUIDeviceInfo_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_RemoteUIDeviceInfo_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI
core_RemoteUIDeviceInfo_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/HttpClient.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/HttpServer.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerHttp.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerWS.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/McpProxyHandler.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/McpServerManager.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/PollListenner.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/WebSocket.$(OBJEXT)                        \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUI.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUIOutputRelay.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/FirmwareManifest.$(OBJEXT)        \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/HMACAuthenticator.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaFirmwareManager.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaHttpHandler.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIManager.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIProvisioningHandler.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIWebSocketHandler.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RemoteUIDeviceInfo_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.2d
#Explicit and unique ownership of the rules (step 4/6 of the E4.2 series, the
#rule-side counterpart of E4.2b): a Rule owns its Conditions/Actions through
#unique_ptr, ListeRule owns its Rules, rules_scenarios is a non-owning index,
#and there is no manual delete left on the rule path. Pins that every accessor
#hands out a NON-OWNING pointer (there is no hand-back path here, unlike
#delete_io(io, del=false) in E4.2b), that a removal destroys exactly once and
#never twice, that ListeRule only destroys what it owns, and - the invariant of
#the series - that a removal leaves the ORDER of the remaining rules and of the
#remaining conditions/actions untouched, since that order is the evaluation
#order. Links the plain core objects only (Rule.o and ListeRule.o are already
#in CORE_SERVER_OBJECTS). Own HAVE_GTEST block so this stays a pure append at
#the end of the file.
if HAVE_GTEST
TESTS += core/RuleOwnership_test
check_PROGRAMS += core/RuleOwnership_test
core_RuleOwnership_test_SOURCES = core/RuleOwnership_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_RuleOwnership_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_RuleOwnership_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RuleOwnership_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.2e
#A rule whose dependency is missing is DISABLED (user decision). Pins the
#danger this ticket exists for - an amputated conjunction is a MORE permissive
#rule - and the four halves of the new contract: the "IO not found" cause is
#told apart from a malformed node (only the first disables), the rule stays
#loaded, visible and COMPLETE while being excluded from every execution path
#(trigger collection, CheckConditions, CheckConditionsAsync, ExecuteActions),
#the save keeps the unresolvable reference and its parameters across a
#save/reload/save cycle, and the diagnosis is reachable without the log
#(ListeRule::getDisabledRules() + the configuration alert). Links the plain
#core objects only. Own HAVE_GTEST block so this stays a pure append at the end
#of the file.
if HAVE_GTEST
TESTS += core/RuleDisabledMissingIo_test
check_PROGRAMS += core/RuleDisabledMissingIo_test
core_RuleDisabledMissingIo_test_SOURCES = core/RuleDisabledMissingIo_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_RuleDisabledMissingIo_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_RuleDisabledMissingIo_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RuleDisabledMissingIo_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.14
#Objects used after their own static destructor ran. StaticLogShutdown_helper
#is a repro program, not a test: it reproduces the pattern (a translation unit
#static of the PROGRAM whose destructor logs, linked against
#libcalaos_common), which cannot be observed from inside a gtest binary since
#the interesting moment is after main() returned. It is listed in
#check_PROGRAMS so that "make check" builds it, and NOT in TESTS.
#StaticLogShutdown_test runs it and inspects its output; it needs no calaos
#library of its own, which also keeps its own statics out of the picture. Own
#HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += StaticLogShutdown_test
check_PROGRAMS += StaticLogShutdown_test StaticLogShutdown_helper
StaticLogShutdown_test_SOURCES = StaticLogShutdown_test.cpp
StaticLogShutdown_test_LDADD = -lgtest -lgtest_main

StaticLogShutdown_helper_SOURCES = StaticLogShutdown_helper.cpp
StaticLogShutdown_helper_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.0a
#Foundation of the JSON API characterization net (series E4.0, before the
#jansson -> nlohmann migration of E4.1). Ships the harness the five following
#sub-tickets build on - the semantic oracle (parse both sides with nlohmann,
#compare with operator==, never compare serialized strings), the golden file
#format and its code-generated update mode, the in-process WS session, the
#single-shot HTTP request over a real HttpClient on an unconnected socket, the
#reference house, and the uvw loop pump events need - plus the sample of cases
#that proves the whole chain against the production code.
#Links the same closure as core/RemoteUIDeviceInfo_test: JsonApiHandlerHttp.o
#needs the REAL HttpClient::buildHttpResponse() (stubbing it would characterize
#the stub, and that formatting is precisely what E4.0e must pin), and HttpClient
#drags the HttpServer/WebSocket/RemoteUI side of the server with it.
#IPCam/StandardMjpeg.o and Audio/RoonPlayer.o are added so the reference house
#can hold a REAL camera and a REAL audio player: buildJsonCameras() and
#buildJsonAudio() filter by dynamic_cast, and on an empty house they answer []
#and stay uncharacterized. Both were picked by measuring their closure with
#nm -Cu: each needs only its base class (IPCam.o, Audio/AudioPlayer.o), which
#CORE_SERVER_OBJECTS already provides, so the cost is ZERO extra objects.
#Squeezebox was rejected: it drags SqueezeboxDB, UrlDownloader and the nine AVR
#objects, and links uv_tcp_connect/uv_write - real network in a unit test.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiCharacterization_test
check_PROGRAMS += core/JsonApiCharacterization_test
core_JsonApiCharacterization_test_SOURCES = core/JsonApiCharacterization_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiCharacterization_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiCharacterization_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/HttpClient.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/HttpServer.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerHttp.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerWS.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/McpProxyHandler.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/McpServerManager.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/PollListenner.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/WebSocket.$(OBJEXT)                        \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUI.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUIOutputRelay.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/FirmwareManifest.$(OBJEXT)        \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/HMACAuthenticator.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaFirmwareManager.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaHttpHandler.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIManager.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIProvisioningHandler.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIWebSocketHandler.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_JsonApiCharacterization_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/ws_get_home.json \
                  core/golden/http_get_home.json
endif

# T3.17a
#Characterization + lifetime net for the RECURSIVE playlist chain of JsonApi
#(decodeGetPlaylist / getNextPlaylistItem). get_playlist is the one common
#operation of the E4.0 plan that no E4.0 sub-ticket ever covers (E4.0.md:38 vs
#the coverage lists of E4.0a..f), so T3.17a brings its own net before adding the
#apiAlive guard to that chain. The cases pin the WHOLE answer - every requested
#index and every item, in order - because the failure mode of a badly placed
#guard on a recursion is a silently truncated playlist, not a crash.
#Reuses the E4.0a harness verbatim (semantic JSON oracle, goldens, in-process WS
#session and single-shot HTTP request over a real HttpClient), hence the same
#link closure and the same -DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiPlaylist_test
check_PROGRAMS += core/JsonApiPlaylist_test
core_JsonApiPlaylist_test_SOURCES = core/JsonApiPlaylist_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiPlaylist_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiPlaylist_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/HttpClient.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/HttpServer.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerHttp.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerWS.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/McpProxyHandler.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/McpServerManager.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/PollListenner.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/WebSocket.$(OBJEXT)                        \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUI.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUIOutputRelay.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/FirmwareManifest.$(OBJEXT)        \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/HMACAuthenticator.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaFirmwareManager.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaHttpHandler.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIManager.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIProvisioningHandler.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIWebSocketHandler.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_JsonApiPlaylist_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/t317a_ws_get_playlist.json \
                  core/golden/t317a_http_get_playlist.json \
                  core/golden/t317a_ws_get_playlist_empty.json \
                  core/golden/t317a_ws_get_playlist_unknown_player.json \
                  core/golden/t317a_http_get_playlist_unknown_player.json
endif

# E4.0b
#Characterization of the model and the state commands of the JSON API - the
#nine every client calls, on BOTH transports: get_home, get_io, get_state,
#get_states, query, get_param, set_param, del_param, set_state. Second
#sub-ticket of the E4.0 series, it builds on the harness E4.0a delivered
#(core/JsonApiCharacterization.{h,cpp}) and only enriches its reference house.
#Same link closure as core/JsonApiCharacterization_test, and for the same
#measured reasons: JsonApiHandlerHttp.o needs the REAL
#HttpClient::buildHttpResponse(), and the reference house carries two real
#cameras (IPCam/StandardMjpeg.o) and one real audio player
#(Audio/RoonPlayer.o) so buildJsonCameras()/buildJsonAudio() are characterized
#on non-empty arrays and get_state exercises its asynchronous player chain.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiHome_test
check_PROGRAMS += core/JsonApiHome_test
core_JsonApiHome_test_SOURCES = core/JsonApiHome_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiHome_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiHome_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/HttpClient.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/HttpServer.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerHttp.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerWS.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/McpProxyHandler.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/McpServerManager.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/PollListenner.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/WebSocket.$(OBJEXT)                        \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUI.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/RemoteUI/RemoteUIOutputRelay.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/FirmwareManifest.$(OBJEXT)        \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/HMACAuthenticator.$(OBJEXT)       \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaFirmwareManager.$(OBJEXT)      \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/OtaHttpHandler.$(OBJEXT)          \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIManager.$(OBJEXT)         \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIProvisioningHandler.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/RemoteUI/RemoteUIWebSocketHandler.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_JsonApiHome_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/e40b_ws_get_io.json \
                  core/golden/e40b_http_get_io.json \
                  core/golden/e40b_ws_get_state.json \
                  core/golden/e40b_http_get_state.json
endif
