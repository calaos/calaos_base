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
# check through a wrapper. PYTHON comes from AM_PATH_PYTHON via
# AM_TESTS_ENVIRONMENT above.
# F-PYTEST-1: the wrapper delegates to python-suite-runner.py, which counts the
# declared cases, counts the executed ones, publishes both, and exits 77 (a
# SKIP that shows in the "# SKIP:" column) rather than 0 when they disagree.
TESTS += run-python-tests.sh
dist_check_SCRIPTS += run-python-tests.sh

# F-PYTEST-1: meta-oracle over the entry just above. run-python-tests.sh used
# to exit 0 -- automake PASS -- while three of the six suites in tests/python/
# had not been executed at all; the only trace was one line in a .log. This
# guard asserts the launcher publishes how many declared cases it really ran
# and that its exit code agrees (all -> 0, some missing -> 77 = a SKIP that
# shows up in the "# SKIP:" column, never a silent PASS). It needs no gtest and
# no built binary, so it lives outside the HAVE_GTEST conditional.
TESTS += check-python-tests-reporting.sh
dist_check_SCRIPTS += check-python-tests-reporting.sh
EXTRA_DIST = \
              python-suite-runner.py         \
              python/conftest.py             \
              python/test_auth.py            \
              python/test_extern_proc.py     \
              python/test_logger.py          \
              python/test_t116_mcp_client.py \
              python/test_t116_mcp_config_io.py \
              python/test_t116_roon.py

# T3.45: dist-path integrity. Every file named by EXTRA_DIST and friends must
# exist, or `make dist` dies with "No rule to make target" and no release
# tarball can be built. src/lib/calaos-python/Makefile.am shipped such a path
# from 2025-02-16 to 2026-08-25 undetected, because CI runs neither dist nor
# distcheck. This is the sub-second static stand-in for that check; the real
# `make distcheck` remains a manual/release-time gate (docs/refactoring/T3.45.md).
TESTS += check-extra-dist.sh
dist_check_SCRIPTS += check-extra-dist.sh
EXTRA_DIST += check-extra-dist.py

# T3.48: the other direction of the same question. check-extra-dist.sh above
# asks "does every declared dist path exist?"; this asks "does every file that
# exists get declared?". `make dist` answered "tarball built" while leaving 417
# tracked files out of it -- 126 C/C++ sources and headers, every vendored
# library's licence, and exprtk.hpp, without which the unpacked archive does
# not compile. It reuses check-extra-dist.py's Makefile.am parser rather than
# growing a second one. docs/refactoring/T3.48.md.
TESTS += check-dist-coverage.sh
dist_check_SCRIPTS += check-dist-coverage.sh
EXTRA_DIST += check-dist-coverage.py

# T3.48: the Autobahn websocket fuzzing harness. Driven by hand, not by
# `make check`, but it is part of the test suite a tarball consumer gets.
EXTRA_DIST += \
              websockets/fuzzingclient.json      \
              websockets/run_testsuite_client.sh \
              websockets/run_testsuite_server.sh

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

# T3.17d
#Characterization + lifetime net for the ONE-SHOT CAMERA SNAPSHOT: the
#get_picture branch of JsonApiHandlerHttp::processCamera(). It is the only site
#of the T3.17 series that lives in a handler instead of JsonApi.cpp, so it is
#the only one not covered transitively by the apiAlive token of the base class.
#Not covered by E4.0 either: E4.0 excludes get_camera_pic and get_cover because
#they spawn calaos_picture, and get_picture - a different action entirely, which
#spawns nothing - is in no sub-ticket coverage list.
#The answer here is image/jpeg, not JSON, so the cases pin the exact bytes of
#the body; the two error branches are JSON and go through the semantic oracle.
#Reuses the E4.0a harness verbatim, hence the same link closure and the same
#-DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test (no golden of its own:
#a jpeg body is not a golden document).
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiCameraSnapshot_test
check_PROGRAMS += core/JsonApiCameraSnapshot_test
core_JsonApiCameraSnapshot_test_SOURCES = core/JsonApiCameraSnapshot_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiCameraSnapshot_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiCameraSnapshot_test_LDADD = \
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
core_JsonApiCameraSnapshot_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.17b
#Characterization + lifetime net for the five SINGLE SHOT player-state methods
#of JsonApi (audioGetDbStats, audioGetPlaylistSize, audioGetTime,
#audioGetPlaylistItem, audioGetCoverInfo), before the apiAlive guard is added to
#them. E4.0f was supposed to cover this surface but is neither delivered nor
#started, so T3.17b brings its own net - same rule as T3.17a.
#The failure mode of a badly placed guard on a single shot is a response that
#never leaves, so every case pins the exact answer document of one method on one
#transport, and the deferred cases check that a live client still gets its
#answer after the round trip.
#Reuses the E4.0a harness verbatim (semantic JSON oracle, goldens, in-process WS
#session and single-shot HTTP request over a real HttpClient), hence the same
#link closure and the same -DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiPlayerState_test
check_PROGRAMS += core/JsonApiPlayerState_test
core_JsonApiPlayerState_test_SOURCES = core/JsonApiPlayerState_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiPlayerState_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiPlayerState_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiPlayerState_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/t317b_ws_audio_get_playlist_size.json \
                  core/golden/t317b_http_audio_get_playlist_size.json \
                  core/golden/t317b_ws_audio_get_time.json \
                  core/golden/t317b_http_audio_get_time.json \
                  core/golden/t317b_ws_audio_get_playlist_item.json \
                  core/golden/t317b_http_audio_get_playlist_item.json \
                  core/golden/t317b_ws_audio_get_cover_url.json \
                  core/golden/t317b_http_audio_get_cover_url.json \
                  core/golden/t317b_ws_audio_db_get_stats.json \
                  core/golden/t317b_http_audio_db_get_stats.json \
                  core/golden/t317b_ws_audio_unknown_player.json \
                  core/golden/t317b_http_audio_unknown_player.json \
                  core/golden/t317b_ws_audio_empty_player_id.json \
                  core/golden/t317b_ws_audio_db_get_stats_unknown_player.json \
                  core/golden/t317b_ws_audio_get_playlist_item_missing_item.json
endif

# E4.0c
#Characterization of the time range and auto scenario commands of the JSON API:
#get_timerange, set_timerange and the seven autoscenario sub-commands. Third
#sub-ticket of the E4.0 series, built on the E4.0a harness
#(core/JsonApiCharacterization.{h,cpp}). It brings its OWN small house
#(InPlageHoraire + scenarios created through the API) and does not touch the
#shared reference house, whose deliberate sparseness E4.0b's counter-mutation
#showed to be load bearing.
#Same link closure as core/JsonApiHome_test, and for the same measured reason:
#JsonApiHandlerHttp.o needs the REAL HttpClient::buildHttpResponse(). The
#camera/audio objects are part of that closure through the shared harness
#object, which instantiates them for the reference house.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiScenario_test
check_PROGRAMS += core/JsonApiScenario_test
core_JsonApiScenario_test_SOURCES = core/JsonApiScenario_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiScenario_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -I$(top_srcdir)/src/bin/calaos_server/Scenario \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiScenario_test_LDADD = \
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
core_JsonApiScenario_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution.
#The two last ones are the UNHEALTHY scenarios T3.18.md asks for (a broken one
#and a repaired one waiting for a manual re-enable). T3.18 regenerates all five
#scenario goldens with CALAOS_GOLDEN_UPDATE=1 when it adds broken /
#disabled_missing_io / missing_ios to Scenario::toJson().
EXTRA_DIST += core/golden/e40c_ws_get_timerange.json \
                  core/golden/e40c_http_get_timerange.json \
                  core/golden/e40c_ws_autoscenario_list.json \
                  core/golden/e40c_ws_autoscenario_get.json \
                  core/golden/e40c_http_autoscenario_get.json \
                  core/golden/e40c_ws_autoscenario_get_scheduled.json \
                  core/golden/e40c_ws_autoscenario_get_hot_deleted.json \
                  core/golden/e40c_ws_autoscenario_get_broken.json \
                  core/golden/e40c_ws_autoscenario_get_repaired.json
endif


# T3.17e
#Websocket TRANSPORT audit: the alive token JsonApiHandlerWS does NOT declare.
#Builds a real WebSocket (which derives from HttpClient, WebSocket.h:35) on an
#unconnected uvw handle, owning its JsonApiHandlerWS through HttpClient::jsonApi
#exactly like WebSocket.cpp:279, and destroys the TRANSPORT while an answer is in
#flight - the path no existing test covers, since WsTestSession of the E4.0a
#harness builds its handler on a null HttpClient and deletes the handler itself.
#Same link closure and same -DCALAOS_GOLDEN_DIR as core/JsonApiPlaylist_test (the
#harness .cpp needs it even when a case compares no golden).
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiWsTransport_test
check_PROGRAMS += core/JsonApiWsTransport_test
core_JsonApiWsTransport_test_SOURCES = core/JsonApiWsTransport_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiWsTransport_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiWsTransport_test_LDADD = \
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
core_JsonApiWsTransport_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.0d
#Characterization of the REAL TIME EVENTS of the JSON API - the surface with
#zero tests before this file: the 23 strings of CalaosEvent::typeToString(),
#the enum numbering that travels on the wire as "type", the WS "event"
#envelope with its nested data.data, the !loggedin gating, the asynchronous
#delivery through the EventManager idler, and the HTTP equivalent poll_listen
#with its three sub-actions. Fourth sub-ticket of the E4.0 series, built on the
#E4.0a harness (core/JsonApiCharacterization.{h,cpp}).
#Same link closure as core/JsonApiHome_test, and for the same measured reasons:
#JsonApiHandlerHttp.o needs the REAL HttpClient::buildHttpResponse(), and the
#reference house of the harness carries two real cameras (IPCam/StandardMjpeg.o)
#and one real audio player (Audio/RoonPlayer.o). PollListenner.o is what makes
#poll_listen testable in process.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiEvents_test
check_PROGRAMS += core/JsonApiEvents_test
core_JsonApiEvents_test_SOURCES = core/JsonApiEvents_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiEvents_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiEvents_test_LDADD = \
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
core_JsonApiEvents_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/e40d_ws_event_envelope.json \
                  core/golden/e40d_ws_event_accented.json \
                  core/golden/e40d_ws_event_catalog.json \
                  core/golden/e40d_ws_clear_state_backlog.json \
                  core/golden/e40d_ws_io_added.json \
                  core/golden/e40d_ws_io_deleted.json \
                  core/golden/e40d_ws_io_changed_bool.json \
                  core/golden/e40d_ws_io_changed_int.json \
                  core/golden/e40d_ws_io_changed_string.json \
                  core/golden/e40d_ws_io_changed_setparam.json \
                  core/golden/e40d_ws_io_prop_deleted.json \
                  core/golden/e40d_ws_room_changed.json \
                  core/golden/e40d_ws_timerange_changed.json \
                  core/golden/e40d_ws_touchscreen_camera.json \
                  core/golden/e40d_http_poll_get.json
endif

# T3.17c
#Characterization + lifetime net for the FIFTEEN audioDbGet* music-database
#methods of JsonApi, before the apiAlive guard is added to them. E4.0f was
#supposed to cover three of them and to leave the twelve others out by design
#(E4.0.md:342-350); it is neither delivered nor started, so T3.17c brings its
#own net for all fifteen - same rule as T3.17a and T3.17b.
#The failure mode of a badly placed guard on a single shot is a response that
#never leaves, so every case pins the exact answer document of one method on one
#transport (thirty goldens for the thirty call sites of the two handlers), the
#three shapes of processDbResult(), the synchronous error gates, and the
#deferred path where a live client still gets its answer after the round trip.
#Reuses the E4.0a harness verbatim (semantic JSON oracle, goldens, in-process WS
#session and single-shot HTTP request over a real HttpClient), hence the same
#link closure and the same -DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test.
#Audio/AudioDB.$(OBJEXT) is needed for the same reason as in T3.17b: the fake
#music database is a concrete AudioDB subclass, and the reference house's
#RoonPlayer carries no database at all.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiMusicDb_test
check_PROGRAMS += core/JsonApiMusicDb_test
core_JsonApiMusicDb_test_SOURCES = core/JsonApiMusicDb_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiMusicDb_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiMusicDb_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiMusicDb_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/t317c_ws_get_albums.json \
                  core/golden/t317c_http_get_albums.json \
                  core/golden/t317c_ws_get_artist_album.json \
                  core/golden/t317c_http_get_artist_album.json \
                  core/golden/t317c_ws_get_year_albums.json \
                  core/golden/t317c_http_get_year_albums.json \
                  core/golden/t317c_ws_get_genre_artists.json \
                  core/golden/t317c_http_get_genre_artists.json \
                  core/golden/t317c_ws_get_album_titles.json \
                  core/golden/t317c_http_get_album_titles.json \
                  core/golden/t317c_ws_get_playlist_titles.json \
                  core/golden/t317c_http_get_playlist_titles.json \
                  core/golden/t317c_ws_get_artists.json \
                  core/golden/t317c_http_get_artists.json \
                  core/golden/t317c_ws_get_years.json \
                  core/golden/t317c_http_get_years.json \
                  core/golden/t317c_ws_get_genres.json \
                  core/golden/t317c_http_get_genres.json \
                  core/golden/t317c_ws_get_playlists.json \
                  core/golden/t317c_http_get_playlists.json \
                  core/golden/t317c_ws_get_music_folder.json \
                  core/golden/t317c_http_get_music_folder.json \
                  core/golden/t317c_ws_get_search.json \
                  core/golden/t317c_http_get_search.json \
                  core/golden/t317c_ws_get_radios.json \
                  core/golden/t317c_http_get_radios.json \
                  core/golden/t317c_ws_get_radio_items.json \
                  core/golden/t317c_http_get_radio_items.json \
                  core/golden/t317c_ws_get_track_infos.json \
                  core/golden/t317c_http_get_track_infos.json \
                  core/golden/t317c_ws_zero_count.json \
                  core/golden/t317c_ws_no_count.json \
                  core/golden/t317c_ws_unknown_player.json \
                  core/golden/t317c_http_unknown_player.json \
                  core/golden/t317c_ws_empty_player_id.json \
                  core/golden/t317c_ws_wrong_from_count.json \
                  core/golden/t317c_ws_track_infos_no_track_id.json \
                  core/golden/t317c_ws_unknown_audio_action.json \
                  core/golden/t317c_http_unknown_audio_action.json
endif

#E4.0e: session, envelopes and error paths of the JSON API - login,
#login_service, settings/change_cred, register_push, get_mcp_info, eventlog,
#config/get, plus every error path of both transports (the seven scope
#refusals, "unknown action", "unkown audio_action", the silences, the HTTP 400
#and 404, and the migration traps: invalid UTF-8, empty strings, very large
#integers, absent members, unexpected types).
#Reuses the E4.0a harness verbatim (semantic JSON oracle, goldens, in-process WS
#session and single-shot HTTP request over a real HttpClient), hence the same
#link closure and the same -DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiSession_test
check_PROGRAMS += core/JsonApiSession_test
core_JsonApiSession_test_SOURCES = core/JsonApiSession_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiSession_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiSession_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiSession_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/e40e_http_audio_db_unknown_action.json \
                  core/golden/e40e_http_camera_unknown_id.json \
                  core/golden/e40e_http_config_unknown_type.json \
                  core/golden/e40e_http_get_camera_pic_invalid_width.json \
                  core/golden/e40e_http_get_camera_pic_unknown_id.json \
                  core/golden/e40e_http_get_cover_unknown_id.json \
                  core/golden/e40e_http_get_mcp_info.json \
                  core/golden/e40e_http_get_param_invalid_utf8_name.json \
                  core/golden/e40e_http_get_param_invalid_utf8_value.json \
                  core/golden/e40e_http_poll_listen_get_unknown_uuid.json \
                  core/golden/e40e_http_poll_listen_unknown_type.json \
                  core/golden/e40e_http_register_push_success.json \
                  core/golden/e40e_http_unknown_action.json \
                  core/golden/e40e_ws_audio_unknown_action.json \
                  core/golden/e40e_ws_eventlog_last_page_is_empty.json \
                  core/golden/e40e_ws_eventlog_page_out_of_range.json \
                  core/golden/e40e_ws_eventlog_uuid_not_found.json \
                  core/golden/e40e_ws_get_param_no_param_member.json \
                  core/golden/e40e_ws_get_state_without_data.json \
                  core/golden/e40e_ws_login_failure.json \
                  core/golden/e40e_ws_login_service_invalid_token.json \
                  core/golden/e40e_ws_login_service_success.json \
                  core/golden/e40e_ws_login_success.json \
                  core/golden/e40e_ws_login_success_no_msg_id.json \
                  core/golden/e40e_ws_register_push_refused.json \
                  core/golden/e40e_ws_register_push_success.json \
                  core/golden/e40e_ws_scope_denied.json \
                  core/golden/e40e_ws_settings_change_cred_refused.json \
                  core/golden/e40e_ws_settings_change_cred_success.json
endif

#------------------------------------------------------------------------------
#E4.0f - the audio surface of the JSON API characterized at VALUE level:
#processDbResult()'s count-marker contract, the scalar formatting of the audio
#answers (Utils::to_string(double) is six significant digits), the empty string
#and non-ASCII rows of the switch table, and the WS/HTTP argument-placement
#asymmetry. Uses the E4.0a harness unchanged; needs Audio/AudioDB.o for the
#concrete fake database, like core/JsonApiPlayerState_test. Same link closure
#and the same -DCALAOS_GOLDEN_DIR as core/JsonApiCharacterization_test.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiAudioPayload_test
check_PROGRAMS += core/JsonApiAudioPayload_test
core_JsonApiAudioPayload_test_SOURCES = core/JsonApiAudioPayload_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiAudioPayload_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiAudioPayload_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiAudioPayload_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/e40f_ws_db_count_marker_in_the_middle.json \
                  core/golden/e40f_ws_db_no_count_marker_at_all.json \
                  core/golden/e40f_ws_db_rejects_the_http_spelling.json \
                  core/golden/e40f_http_db_rejects_the_ws_spelling.json \
                  core/golden/e40f_http_db_count_marker_in_the_middle.json \
                  core/golden/e40f_ws_db_last_count_marker_wins.json \
                  core/golden/e40f_ws_db_count_double_zero_does_not_clear.json \
                  core/golden/e40f_ws_db_trailing_zero_count_clears.json \
                  core/golden/e40f_ws_db_count_not_a_number.json \
                  core/golden/e40f_ws_db_count_marker_row_keeps_its_other_keys.json \
                  core/golden/e40f_ws_audio_time_six_significant_digits.json \
                  core/golden/e40f_ws_audio_time_large_value_goes_scientific.json \
                  core/golden/e40f_ws_audio_playlist_size_negative.json \
                  core/golden/e40f_ws_audio_cover_url_empty.json \
                  core/golden/e40f_ws_audio_playlist_item_non_ascii.json \
                  core/golden/e40f_http_audio_playlist_item_non_ascii.json \
                  core/golden/e40f_ws_db_track_infos_non_ascii.json \
                  core/golden/e40f_ws_audio_http_shaped_request.json \
                  core/golden/e40f_http_audio_ws_shaped_request.json \
                  core/golden/e40f_http_audio_db_ws_shaped_arguments.json
endif

#T3.17f: eventlog - the last unguarded async chain of JsonApi.cpp
#(buildJsonEventLog(), the 26th and only std::function<void(Json &)> method of
#JsonApi.h, found by the T3.17e audit). Characterizes both of its callbacks - the
#uuid one and the paginated one - on both transports, then pins the lifetime
#guard: the JsonApi/handler destroyed while the sqlite worker thread is still
#running the query.
#Drives HistLogger like core/JsonApiSession_test (E4.0e) does: the history
#database is built ONCE, before the first CoreFixture::SetUp(), in a directory
#that lives as long as the process - a singleton born inside a case would point
#at a per-case cache directory that TearDown() rm -rf's, and the worker would
#then throw sqlite cantopen OUTSIDE its try block (HistLogger.cpp:190) and
#std::terminate the binary. Different directory template from E4.0e's so the two
#binaries can never share a database.
#HistLogger.$(OBJEXT) is already part of CORE_SERVER_OBJECTS, so the link closure
#is the same as core/JsonApiCharacterization_test, and so is -DCALAOS_GOLDEN_DIR.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiEventLog_test
check_PROGRAMS += core/JsonApiEventLog_test
core_JsonApiEventLog_test_SOURCES = core/JsonApiEventLog_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiEventLog_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiEventLog_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiEventLog_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/t317f_ws_eventlog_page_out_of_range.json \
                  core/golden/t317f_ws_eventlog_uuid_not_found.json \
                  core/golden/t317f_http_eventlog_uuid_not_found.json
endif

#T3.19: the two REMOTE CRASHES of JsonApi.cpp reachable by an authenticated
#client on a legitimate call - the null AudioPlayer::database dereferenced by
#the sixteen audio_db actions (SIGSEGV) and the unvalidated per_page divided by
#in HistLogger's sqlite worker (SIGFPE). One binary because both are the same
#defect in the same file, and because the eventlog half needs HistLogger brought
#into the harness anyway.
#Audio/AudioDB.$(OBJEXT) is needed for the concrete database the "capability
#says no, pointer says yes" cases install (same need as T3.17c/E4.0f), and the
#history database is built ONCE before the first CoreFixture::SetUp() against a
#directory that lives as long as the process - a singleton born inside a case
#would point at a per-case cache directory TearDown() rm -rf's, and
#HistLogger.cpp:190 sits OUTSIDE its try, so cantopen would std::terminate the
#binary. Own directory template (calaos_t319_hist_XXXXXX), so this binary can
#never share a database with core/JsonApiSession_test or core/JsonApiEventLog_test.
#HistLogger.$(OBJEXT) is already part of CORE_SERVER_OBJECTS, so the link
#closure is the same as core/JsonApiMusicDb_test, and so is -DCALAOS_GOLDEN_DIR.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiInputGuards_test
check_PROGRAMS += core/JsonApiInputGuards_test
core_JsonApiInputGuards_test_SOURCES = core/JsonApiInputGuards_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiInputGuards_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiInputGuards_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiInputGuards_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
#Goldens are data, they must travel with the distribution
EXTRA_DIST += core/golden/t319_ws_audio_db_no_database.json \
                  core/golden/t319_http_audio_db_no_database.json \
                  core/golden/t319_ws_eventlog_per_page_out_of_range.json \
                  core/golden/t319_http_eventlog_per_page_out_of_range.json
endif


# T3.18
#A scenario whose step lost its IO is DISABLED (user decision), with a
#persisted, sticky `disabled_missing_io` param and a manual re-enable. Core
#test: it plays real save/reload cycles, which is the only way the stickiness
#can be shown, and drives buildAutoscenarioModify()/buildAutoscenarioReenable()
#directly through a subclass of JsonApi - JsonApi.o is already part of
#CORE_SERVER_OBJECTS, so no extra link closure is needed here (no WS/HTTP
#transport is involved).
#Own HAVE_GTEST block so that this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/ScenarioDisabledMissingIo_test
check_PROGRAMS += core/ScenarioDisabledMissingIo_test
core_ScenarioDisabledMissingIo_test_SOURCES = core/ScenarioDisabledMissingIo_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ScenarioDisabledMissingIo_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/Scenario
core_ScenarioDisabledMissingIo_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ScenarioDisabledMissingIo_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif


# E4.6a
#CHARACTERIZATION of the AutoScenario model, written before a single line of
#src/ is touched by the E4.6 rewrite. It pins the five defects E4.6.md asks for
#- the orphan sweep that destroys the rules of an unmarked scenario, the plain
#IO an unmarked scenario IO stays, the "wrong input" of autoscenario get, the
#full UI round trip that restarts an amputated scenario with two success:true,
#and the installer style reload that leaves no trace at all - plus the one
#acquired property nothing attested: config put backs the configuration up
#BEFORE overwriting it.
#It drives BOTH transports (the reference use case over WS, config put over
#HTTP) and plays real save/reload cycles, so it needs the E4.0a harness and the
#same link closure as core/JsonApiScenario_test: JsonApiHandlerHttp.o needs the
#REAL HttpClient::buildHttpResponse(), and the camera/audio objects come with
#the shared harness object.
#It compares no golden, but the harness .cpp still needs -DCALAOS_GOLDEN_DIR.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/AutoScenarioMigration_test
check_PROGRAMS += core/AutoScenarioMigration_test
core_AutoScenarioMigration_test_SOURCES = core/AutoScenarioMigration_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_AutoScenarioMigration_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -I$(top_srcdir)/src/bin/calaos_server/Scenario \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_AutoScenarioMigration_test_LDADD = \
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
core_AutoScenarioMigration_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif


# E4.1a
#Characterization of the two JSON faces of Params, written BEFORE the bridge
#is cut and green across the cut. Params.h is the one header that includes
#both jansson and nlohmann; E4.1a keeps only the nlohmann face on the class
#and moves the jansson serialization, verbatim, into the transitional adapter
#jansson_from_params() of src/lib/Jansson_Addition.h. The only line of the
#test that the migration touches is the seam paramsToJansson(); no assertion
#moves. Only libcalaos_common code is exercised (Params + Jansson_Addition),
#no server object and no fixture. Own HAVE_GTEST block so this stays a pure
#append at the end of the file.
if HAVE_GTEST
TESTS += ParamsJson_test
check_PROGRAMS += ParamsJson_test
ParamsJson_test_SOURCES = ParamsJson_test.cpp
ParamsJson_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

#T3.24: which client the login throttle counts against. Needs a real HttpClient
#whose request_headers carry an X-Forwarded-For, which the E4.0a harness does
#not model (its WS session is built on a null HttpClient and its HTTP request on
#a header-less one), so the two proxied session helpers live in the test file
#itself and the harness is reused unmodified for the fixture and the oracle.
#Same link closure as core/JsonApiSession_test - both drive both transports.
#No golden: every assertion here is a boolean about which bucket was hit.
if HAVE_GTEST
TESTS += core/JsonApiThrottleIdentity_test
check_PROGRAMS += core/JsonApiThrottleIdentity_test
core_JsonApiThrottleIdentity_test_SOURCES = core/JsonApiThrottleIdentity_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiThrottleIdentity_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiThrottleIdentity_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiThrottleIdentity_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif


# E4.1k
#Characterization of the io_doc.json generator: IODoc::genDocJson() (the
#document of one IO type) and IOFactory::genDocIO() (the aggregation and the
#write of io_doc.json/io_doc.md). Written BEFORE the jansson -> nlohmann
#migration of those two functions and green across it; the single seam is
#docToJson() in the test itself, no assertion moves.
#It needs the server core objects for the second half (genDoc instantiates
#every registered IO type), so it uses the core LDADD, and jansson for the
#seam, which @CALAOS_COMMON_LIBS@ already carries. Own HAVE_GTEST block so
#this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/IODocJson_test
check_PROGRAMS += core/IODocJson_test
core_IODocJson_test_SOURCES = core/IODocJson_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_IODocJson_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_IODocJson_test_LDADD = $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_IODocJson_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif


# E4.1e
#Characterization of the KNX wire, written BEFORE IO/KNX is ported from
#jansson to nlohmann::json and green across the port. Nothing in the existing
#suite executes a driver (E4.0d), and the 145 goldens compare parsed
#documents rather than bytes, so this wire had no net at all on either
#dimension. Two binaries because KNXValue is declared TWICE - once in
#KNXCtrl.h for the server, once in KNXExternProc_main.h for calaos_knx - and
#the two classes cannot share a translation unit. They pin the SAME expected
#literals on purpose: the two copies of toJson() must emit the same bytes or
#the two ends of the wire stop understanding each other.
#
#Linking follows core/KnxIo_test: the server objects are reused as they are.
#Only KNXCtrl.o and ExternProc.o are needed here, no fixture and no core.
if HAVE_GTEST
TESTS += KNXCtrlWire_test
check_PROGRAMS += KNXCtrlWire_test
KNXCtrlWire_test_SOURCES = KNXCtrlWire_test.cpp
KNXCtrlWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/KNX \
                  @CALAOS_SERVER_CFLAGS@
KNXCtrlWire_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT) \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
KNXCtrlWire_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

#The calaos_knx end. KNXExternProc_cli.cpp holds the THIRD definition of
#KNXValue::toJson() and is only compiled when eibclient.h is present
#(configure.ac:135 AC_CHECK_HEADERS -> HAVE_LIBKNX); there is no --with-knx
#option, the support is header-detected. Without that header calaos_knx does
#not exist and neither can this test, hence the nested conditional. A build
#that skips it is a build that never compiled two of the five files of this
#ticket: check the configure summary line "Eib/KNX support".
if HAVE_GTEST
if HAVE_LIBKNX
TESTS += KNXExternProcWire_test
check_PROGRAMS += KNXExternProcWire_test
KNXExternProcWire_test_SOURCES = KNXExternProcWire_test.cpp
KNXExternProcWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/KNX \
                  @CALAOS_SERVER_CFLAGS@
KNXExternProcWire_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXExternProc_cli.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT) \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  -leibclient \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
KNXExternProcWire_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
endif


# E4.1g
#Characterization of the MQTT wire (MqttCtrl.cpp <-> MqttExternProc_main.cpp),
#written BEFORE any line of src/ and green across the migration. Nothing in
#the suite executes a driver (E4.0d), so this file is the only net this
#migration has. It pins the payload LENGTH (json_stringn vs json_string), the
#'?' substitution of invalid UTF-8 bytes (a shipped user decision), the
#crossing of a VALID non-ASCII payload, and - on BYTES, not on a parsed
#document - the exact wire produced by both ends, which is the only oracle
#ensure_ascii will ever have. Only libcalaos_common code is exercised (Params
#+ Utils), no server object and no fixture; the migration commit points the
#test at src/bin/calaos_server/IO/Mqtt/MqttWire.h. Own HAVE_GTEST block so
#this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += MqttWire_test
check_PROGRAMS += MqttWire_test
MqttWire_test_SOURCES = MqttWire_test.cpp
MqttWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Mqtt
MqttWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

#E4.1b: the BYTES the API puts on the wire - the one dimension the 145 goldens
#of E4.0 are blind to by construction (they compare parsed documents, so an
#escaping change cannot make any of them red; the E4.1k review measured
#ensure_ascii true->false leaving the whole suite green). Every assertion here
#reads HttpTestRequest::body() / WsTestSession::lastMessage() raw.
#Drives `eventlog`, the only action whose payload is built with nlohmann on both
#transports (buildJsonEventLog(), the single std::function<void(Json &)> method
#of JsonApi.h), so both nlohmann emitters are exercised end to end; and get_home
#in the same case for the jansson side, which is what puts the two escapings
#side by side on one house.
#HistLogger is brought in exactly like core/JsonApiEventLog_test and
#core/JsonApiSession_test do - built once, before the first CoreFixture::SetUp(),
#in a directory of its own (template calaos_e41b_hist_XXXXXX) so no two binaries
#ever share a database.
#Same link closure as core/JsonApiEventLog_test: it drives both transports and
#needs HistLogger, already part of CORE_SERVER_OBJECTS.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/JsonApiEmissionBytes_test
check_PROGRAMS += core/JsonApiEmissionBytes_test
core_JsonApiEmissionBytes_test_SOURCES = core/JsonApiEmissionBytes_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_JsonApiEmissionBytes_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_JsonApiEmissionBytes_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_JsonApiEmissionBytes_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.1i
#Characterization of the Reolink wire: the {action:"register", ...} message
#ReolinkCtrl sends to calaos_reolink, and the flattening of every message the
#external process sends back. Written BEFORE the jansson -> nlohmann migration
#of ReolinkCtrl.cpp and green across it; the two seams are buildRegisterWire()
#and decodeWire() in the test itself, which the migration commit rewires onto
#the production header IO/Reolink/ReolinkWire.h so that a mutation of the
#shipped emitter turns this suite red.
#Two of its cases assert on the RAW BYTES of the wire (ensure_ascii, and the
#invalid-UTF-8 error handler): the 145 goldens compare parsed documents and
#are blind to escaping by construction.
#Only libcalaos_common code is exercised (Params + the free functions of the
#wire header), no server object and no fixture; jansson comes with
#@CALAOS_COMMON_LIBS@. Own HAVE_GTEST block so this stays a pure append at the
#end of the file.
if HAVE_GTEST
TESTS += ReolinkWire_test
check_PROGRAMS += ReolinkWire_test
ReolinkWire_test_SOURCES = ReolinkWire_test.cpp \
                  $(top_srcdir)/src/bin/calaos_server/IO/Reolink/ReolinkWire.h
ReolinkWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Reolink
ReolinkWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# E4.1h
#Characterization of the Wago wire: the eight requests WagoMap sends to
#calaos_wago, the two reply shapes calaos_wago sends back, and the decoding
#both ends do. Written BEFORE the jansson -> nlohmann migration of
#WagoMap.cpp / WagoExternProc_main.cpp and green across it; the seams are the
#free functions of the anonymous namespace in the test itself, which the
#migration commit rewires onto the production header
#IO/Wago/WagoWire.h so that a mutation of the SHIPPED emitter turns this suite
#red. Nothing in this suite executed a driver before (E4.0d), and
#WagoConfigParse_test only covers the io.xml address parser: this file is the
#only net this migration has.
#Two of its cases assert on the RAW BYTES (ensure_ascii, and the invalid-UTF-8
#error handler); the 145 goldens compare parsed documents and are blind to
#escaping by construction. Eight more freeze the exact request strings, which
#do NOT move across the migration - Params is a std::map, so the jansson
#adapter already emitted alphabetically.
#Two of its cases were written as ..._BUG, pinning the fact that
#write_multiple_bits/write_multiple_words emitted no "values" array at all;
#the fix commit of this same ticket flips them, which is what they were for.
#Only libcalaos_common code is exercised (Params + Utils + the wire header),
#no server object and no fixture; jansson comes with @CALAOS_COMMON_LIBS@ and
#-I.../IO/Wago is already in AM_CPPFLAGS. Own HAVE_GTEST block so this stays a
#pure append at the end of the file.
if HAVE_GTEST
TESTS += WagoWire_test
check_PROGRAMS += WagoWire_test
WagoWire_test_SOURCES = WagoWire_test.cpp
WagoWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# E4.1c
#The two build-only contracts the "dead jansson residues" cleanup rests on,
#plus the source tripwires that keep the deletions deleted.
#(A) IO/ExternProc.h must go on handing <jansson.h> to the ten units that take
#it from there and nowhere else; the deleted `#include <jansson.h>` was
#redundant with the `#include "Jansson_Addition.h"` on the next line, and it is
#E4.1x, not E4.1c, that removes the provision itself.
#(B) json_array_foreach is native to jansson >= 2.5 (configure.ac:51), so the
##ifndef compat copy deleted from HttpClient.cpp could never fire.
#This unit includes ExternProc.h and NOTHING else from calaos, so that jansson
#being visible below proves that header brought it: no server object is linked
#and none may be, which also keeps it clear of the _DEPENDENCIES pitfall.
#CALAOS_TOP_SRCDIR lets the tripwire cases read the shipped sources, the same
#way CALAOS_GOLDEN_DIR feeds the golden comparisons.
#OWNED BY E4.1x: this suite calls the jansson C API on purpose and pins the
#Jansson_Addition.h delegation E4.1x removes. It is deleted with jansson, not
#ported.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += JanssonResidues_test
check_PROGRAMS += JanssonResidues_test
JanssonResidues_test_SOURCES = JanssonResidues_test.cpp
JanssonResidues_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -DCALAOS_TOP_SRCDIR='"$(abs_top_srcdir)"'
JanssonResidues_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
JanssonResidues_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.1d
#Characterization of the two THIRD PARTY JSON READERS of the tree: the
#Logitech Media Server answer Squeezebox.cpp walks for artwork_url, and the
#Philips Hue bridge answer HueOutputLightRGB.cpp walks for
#state.{sat,bri,hue,on,reachable}. Both are READ ONLY - neither builds JSON for
#its third party, so neither carries a wire this series has to protect.
#Written BEFORE the jansson -> nlohmann migration of the two files and green
#across it; the seams are the free functions of the anonymous namespace in each
#test, which the migration commit rewires onto the production headers
#Audio/SqueezeboxWire.h and IO/Hue/HueWire.h so that a mutation of the SHIPPED
#reader turns these suites red.
#SqueezeboxWire_test carries the ONLY byte oracles of the ticket, on the ONLY
#dump() of the perimeter - a cDebug() trace: one case is red if ensure_ascii is
#dropped, one is red if error_handler_t::replace is dropped, and a tripwire
#records the measurement that makes that handler DEFENSIVE and not load
#bearing (both libraries refuse invalid UTF-8 at parse time, so nothing can
#reach the dump with a bad byte). The 145 goldens compare parsed documents and
#are blind to escaping by construction.
#HueWire_test carries NO byte oracle and claims none: there is no dump() in
#HueOutputLightRGB.cpp at all. Its risk is TYPE TOLERANCE, and its tripwire
#pins the measured divergence between json_integer_value() and the "obvious"
#j.value("sat", 0), which THROWS on two of the five shapes a bridge can send -
#inside a download callback with no try/catch on the path.
#Only libcalaos_common code is exercised (Params + Utils + ColorValue + the
#wire headers), no server object and no fixture; jansson comes with
#@CALAOS_COMMON_LIBS@. -I.../Audio is already in AM_CPPFLAGS, -I.../IO/Hue is
#not. Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += SqueezeboxWire_test
check_PROGRAMS += SqueezeboxWire_test
SqueezeboxWire_test_SOURCES = SqueezeboxWire_test.cpp
SqueezeboxWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main

TESTS += HueWire_test
check_PROGRAMS += HueWire_test
HueWire_test_SOURCES = HueWire_test.cpp
HueWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Hue
HueWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# E4.1f
#Characterization of the OLA wire: the [{channel,value}, ...] array OLACtrl
#sends to calaos_ola, and the decoding calaos_ola does of it. Written BEFORE
#the jansson -> nlohmann migration of OLACtrl.cpp / OLAExternProc_main.cpp and
#green across it; the seam is the free functions of the anonymous namespace in
#the test itself, which the migration commit rewires onto the production header
#IO/OLA/OLAWire.h so that a mutation of the SHIPPED emitter turns this suite
#red. Nothing in this suite executed a driver before (E4.0d) and there is no
#other OLA test at all: this file is the only net this migration has.
#⭐ This is the ONLY wire of the series that carries real JSON INTEGERS
#(json_integer, not json_string). Its most important case is the type proof -
#a `value` that comes back quoted stops the DMX driver in silence, and the
#decoder cannot catch it because it flattens everything to strings anyway.
#Two of its cases assert on the RAW BYTES (ensure_ascii, and the invalid-UTF-8
#error handler); the 145 goldens compare parsed documents and are blind to
#escaping by construction. Those two are DEFENSIVE and say so: no string of
#any kind reaches this wire in production, only two integers.
#Only libcalaos_common code is exercised (Params + Utils + ColorValue + the
#wire header), no server object and no fixture, so the _DEPENDENCIES pitfall
#cannot apply; jansson comes with @CALAOS_COMMON_LIBS@. -I.../IO/OLA is NOT in
#the global AM_CPPFLAGS, hence the dedicated _CPPFLAGS.
#NOTE: this test does NOT need libola and is built whether or not calaos_ola
#is (if HAVE_LIBOLA in src/bin/calaos_server/Makefile.am) - the wire header has
#no libola dependency. A build without libola still runs this suite, but it
#never compiles OLAExternProc_main.cpp: check the configure summary line
#"Open Lightning Architecture support" before trusting a green.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += OLAWire_test
check_PROGRAMS += OLAWire_test
OLAWire_test_SOURCES = OLAWire_test.cpp
OLAWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/OLA
OLAWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif
# T3.29
#JSON `path` syntax of the MQTT and Web IOs: (A) characterization of the two
#duplicated parsers - an array index is a path segment of its own
#(weather/[0]/description), an index glued to its key resolves to nothing -
#and (B) the syntax the ioDoc actually PUBLISHES, read back out of
#IODoc::genDocJson(), the document --gendoc writes and calaos_installer ships.
#Links the two parser objects from the server build plus WebDocBase (the Web
#`path` description) and the core closure; MqttCtrl::commonDoc() is static and
#WebDocBase has no state, so (B) instantiates no IO. -I.../IO/Mqtt is NOT in
#the global AM_CPPFLAGS, hence the dedicated _CPPFLAGS (same as MqttIO_test).
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
#
#⚠️⚠️ RELINK HOLE - READ THIS BEFORE ANY MUTATION CAMPAIGN ON THIS TARGET.
#Pre-existing, introduced by T3.29, and it belongs to T3.36's scope - but it
#bites HERE, so it is written HERE, where the next person will look.
#_DEPENDENCIES below is overridden to libcalaos_common.la ALONE, so the three
#objects this binary links - MqttCtrl.o, WebCtrl.o, WebDocBase.o - are NOT
#prerequisites of it: `make check` does NOT relink when one of those .cpp
#changes. MEASURED on fix/t3.37, `make -j12 && make check -j6`, same image:
#  1. mutate MqttCtrl.cpp, rm -f binary  -> CXXLD 1, exit 1, 3 reds  (honest)
#  2. restore PRISTINE MqttCtrl.cpp, NO rm -> CXXLD 0, MqttCtrl.o IS rebuilt,
#     and the binary still fails with the SAME 3 reds. 88/87/1 on a CLEAN
#     TREE. ⭐ That is a FALSE RED, and the reviewer of T3.37 walked into it.
#  3. rm -f binary + test .o, rebuild   -> CXXLD 1, 63/63 green.
#⭐ JsonPath.h is NOT in the hole - and that protection is INCIDENTAL, not
#designed. JsonPathSyntax_test.cpp does `#include "JsonPath.h"`, so the header
#lands in THIS target's own .deps: editing it rebuilds the test object, which
#drags the relink along (measured: edit JsonPath.h, no rm -f at all -> CXXLD 1,
#exit 1, 2 reds, one per copy of the parser). ⚠️ Drop that include - e.g. to
#test only through the two wrappers - and the header drops straight into the
#hole beside the two .cpp files. Do not rely on it; rely on the rule below.
#⇒ RULE for this target: rm -f tests/JsonPathSyntax_test AND
#tests/JsonPathSyntax_test-JsonPathSyntax_test.o before every mutation AND
#before every RESTORATION, then require the line `CXXLD    JsonPathSyntax_test`
#(DOUBLE space, path relative to tests/) in the make output, and take the
#verdict from the exit code of the binary run directly.
#⛔ Do NOT "fix" the _DEPENDENCIES line here without measuring. It is T3.36's
#scope; this target is 1 of 51 in the same shape (52 overrides over 87
#check_PROGRAMS, 51 of which really do link server objects - ⚠️ a naive scan
#says 31 because CORE_TEST_LDADD expands to CORE_SERVER_OBJECTS); and a hasty
#harness edit is exactly what produced the nine false green/red variants of
#this series. See FINDINGS.md [F-RELINK-T337].
if HAVE_GTEST
TESTS += JsonPathSyntax_test
check_PROGRAMS += JsonPathSyntax_test
JsonPathSyntax_test_SOURCES = JsonPathSyntax_test.cpp
JsonPathSyntax_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/Mqtt
JsonPathSyntax_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Mqtt/MqttCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Web/WebDocBase.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
JsonPathSyntax_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.1j
#Characterization of the DOWNSTREAM half of the Lua wire: the messages
#calaos_script emits ({msg,data} from every Lua binding that talks back, plus
#the flat "finished") and the messages it decodes ("execute" with its script,
#its context array and its env; "event" with its type_str and its nested
#data). Written BEFORE the jansson -> nlohmann migration of
#LuaScript/ScriptExtern_main.cpp and LuaScript/ScriptBindings.cpp and green
#across it; the seams are the free functions of the anonymous namespace in the
#test itself, which the migration commit rewires onto the production header
#LuaScript/ScriptWire.h so that a mutation of the SHIPPED emitter or decoder
#turns this suite red. LuaScript/ScriptExec.cpp - the calaos_server end - is
#OUT of perimeter (E4.1m) and untouched: both ends decode with a real JSON
#parser, so the mixed state is supported.
#Nothing in this suite executed a driver before (E4.0d) and no golden covers
#this wire: this file is the only net this migration has.
#Three of its cases assert on the RAW BYTES (ensure_ascii, the invalid-UTF-8
#error handler, and the three spellings of that handler pinned separately).
#Unlike Wago/OLA/Hue those oracles are LOAD BEARING here, not defensive: one
#line of user Lua - calaos.sendPushNotif(string.char(0xFF)) - puts an
#arbitrary byte straight into a JSON string value, and a naked dump() on this
#path is std::terminate on calaos_script.
#Only libcalaos_common code is exercised (Params + Utils + the wire header),
#no server object and no fixture, which also keeps it clear of the
#_DEPENDENCIES pitfall; jansson comes with @CALAOS_COMMON_LIBS@.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += ScriptWire_test
check_PROGRAMS += ScriptWire_test
ScriptWire_test_SOURCES = ScriptWire_test.cpp
ScriptWire_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/LuaScript
ScriptWire_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.30
#Characterization of the modbus multiple-write buffer assembly of
#IO/Wago/WagoCtrl.cpp - FINDINGS.md F-WAGO-2, three defects on the same ten
#lines: a `count` taken from the message and never confronted with the decoded
#`values` (SIGSEGV 139 on an empty vector), a setBit() that takes a reference
#to ONE byte and is called with `*data` for every bit (only bits 0..7 are ever
#written, and pos >= 32 is an undefined shift), and a memset shorter than its
#allocation (nb = 17 allocates 3 bytes and zeroes 2).
#⭐ Nothing in the tree calls these functions - WagoMap::write_multiple_bits and
#write_multiple_words have zero callers, indirect ones included, so no PLC has
#ever seen this message and no RELEASE_NOTES entry is due. The suite exists
#because the code LOOKS right: the first caller written gets all three at once.
#THE SEAM is namespace `seam` in the test. The characterization commit carries
#the shipped bodies there verbatim; the fix commit rewires it onto the
#production header IO/Wago/WagoBits.h so that from then on a mutation of the
#SHIPPED packer turns this suite red.
#⚠️ WagoCtrl.o is in NO test binary and cannot be - write_multiple_bits()
#returns on !is_connected() before its body, which needs a live modbus socket.
#The two call sites in WagoCtrl.cpp are covered by SOURCE TRIPWIRES (the
#JanssonResidues_test pattern, hence CALAOS_TOP_SRCDIR) and by nothing else;
#swapping `address` and `nb` there stays green here and needs distinct types,
#which is T3.31's business.
#⚠️ Two of its cases DIE instead of failing before the fix - SIGSEGV on
#values[0], exit code -11, and NO "FAILED" line at all:
#the verdict is the per-case EXIT CODE, taken with --gtest_filter.
#WagoBits.h is a self-contained header (<vector> alone) with no calaos
#dependency; -I.../IO/Wago is already in AM_CPPFLAGS. No server object is
#linked and none may be, which keeps this suite clear of the _DEPENDENCIES
#pitfall. Own HAVE_GTEST block so this stays a pure append at the end of file.
if HAVE_GTEST
TESTS += WagoBits_test
check_PROGRAMS += WagoBits_test
WagoBits_test_SOURCES = WagoBits_test.cpp
WagoBits_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -DCALAOS_TOP_SRCDIR='"$(abs_top_srcdir)"'
WagoBits_test_LDADD = @CALAOS_COMMON_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.28
#Characterization of the Roon launch arguments: the "port" parameter of the
#RoonPlayer ioDoc, and the two startProcess() call sites of RoonCtrl.
#Written BEFORE the fix and RED on master, on three counts: port is declared
#mandatory with no default (9330 landed in the bool `mandatory` parameter of
#IODoc::paramAdd, IODoc.h:46), the processExited handler relaunches
#calaos_roon without --host/--port, and RoonPlayer::port has no in-class
#initialiser.
#⛔ THE NEXT SENTENCE IS FALSE AND IS KEPT ONLY TO BE CORRECTED - see the T3.28b
#paragraph at the end of this block. It read: "RoonCtrl is UNREACHABLE from make
#check - its constructor binds a unix socket through ExternProcServer and spawns
#calaos_roon (measured in E4.1h, FINDINGS.md:109-113, and the review closed the
#question)". Every clause about the constructor is true; the conclusion is not.
#The respawn and the member initialiser were therefore pinned by SOURCE
#TRIPWIRES over CALAOS_TOP_SRCDIR, the same mechanism JanssonResidues_test uses;
#what was already behavioural is the ioDoc, read off a real RoonPlayer, and the
#RoonArgs free functions the fix extracts into Audio/RoonArgs.h.
#It links Audio/RoonPlayer.$(OBJEXT) on top of the core objects. ⛔ T3.27: this
#comment used to say "the only suite of the tree that does", which is FALSE -
#17 targets link that object on fb9d064c, 16 of them core/JsonApi* suites that
#all predate this one. What IS true is that this is the only suite that EXERCISES
#RoonPlayer; the others link it to satisfy the core linker and never call it.
#Linking is not exercising - see FINDINGS.md F-LINK-1.
#⚠️ _DEPENDENCIES = libcalaos_common.la, like 49 of the 83 test binaries, so
#automake does NOT relink this binary when RoonPlayer.o changes: any mutation
#campaign on this file must rm -f BOTH the .o and the test binary and then check
#the CXXLD line really came back.
#⭐ T3.28b: and this suite now EXERCISES RoonCtrl too - it points
#CALAOS_BIN_PREFIX at a temporary directory and watches the real spawn, first
#launch and respawn. "RoonCtrl is unreachable from make check" was false.
#⚠️ It spends the ONE launch this process can observe on a direct
#RoonCtrl::Instance() call, which leaves Audio/RoonPlayer.cpp:227 uncovered -
#that is what core/RoonSpawnViaPlayer_test is for, in its own process. The
#shared harness is core/RoonSpawnHarness.h; it also gives back the /tmp sandbox,
#the unix socket and the last child, none of which _exit() would.
#-I.../Audio is already in AM_CPPFLAGS.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/RoonArgs_test
check_PROGRAMS += core/RoonArgs_test
core_RoonArgs_test_SOURCES = core/RoonArgs_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h \
                  core/RoonSpawnHarness.h
core_RoonArgs_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -DCALAOS_TOP_SRCDIR='"$(abs_top_srcdir)"'
core_RoonArgs_test_LDADD = $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RoonArgs_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.27
#The RETURN CONTRACT of the calaos:* scriptable API, observed FROM LUA.
#setIOParam()/waitForIO() declared `return 1` and pushed nothing, so Lunar::
#thunk handed the script back its own last argument; the defect is a property
#of the lua stack, not of the C++ return value, so every case here pushes a
#real lua_State through ScriptManager::ExecuteScript() and runs literal lua.
#Same object reuse as LuaSandbox_test (ScriptManager/ScriptBindings/ExternProc
#are built into calaos_script, a program): this suite therefore DOES reach
#ScriptBindings.cpp, which FINDINGS.md F-LUA-3 believed unreachable.
#ExternProc.$(OBJEXT) is needed for real here and not only to satisfy the
#linker: one case connects a live AF_UNIX socket and decodes the set_param
#frame the binding emits, which is what turns the io.set_param(value,key)
#permutation red.
#⚠️ _DEPENDENCIES carries libcalaos_common.la only (T3.36): a change to
#ScriptBindings.cpp does not relink this binary on its own.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += LuaCalaosApi_test
check_PROGRAMS += LuaCalaosApi_test
LuaCalaosApi_test_SOURCES = LuaCalaosApi_test.cpp
LuaCalaosApi_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
LuaCalaosApi_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptManager.$(OBJEXT)   \
                  $(CALAOS_SERVER_BUILDDIR)/LuaScript/ScriptBindings.$(OBJEXT)  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/ExternProc.$(OBJEXT)             \
                  @CALAOS_COMMON_LIBS@ \
                  @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
LuaCalaosApi_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.34
#"impulse down <ms>" prefix removal: both shutter IOs recognised the 13
#character prefix and then erased 11 characters, so the duration reached
#Utils::from_string() as "n <ms>" and ImpulseDown() always got 0. Pins the
#duration actually transmitted, the duration published in the IO command
#state, and the stop timer really armed (this one drives the default uvw
#loop, every wait wall clock bounded like core/Timer_test). Links the two
#shutter objects on top of the core closure. Own HAVE_GTEST block so this
#stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/ShutterImpulse_test
check_PROGRAMS += core/ShutterImpulse_test
core_ShutterImpulse_test_SOURCES = core/ShutterImpulse_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ShutterImpulse_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ShutterImpulse_test_LDADD = $(CORE_TEST_LDADD) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutter.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutterSmart.$(OBJEXT)
#Built elsewhere, do not let automake turn them into prerequisites
core_ShutterImpulse_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.28b
#⭐ THE SECOND BINARY, and it is a second BINARY for one measured reason.
#RoonCtrl::Instance() is a function-local static (Audio/RoonPlayer.cpp:86): the
#first call in a process wins, so ONE process can observe exactly ONE launch
#configuration. core/RoonArgs_test spends it by calling Instance() itself,
#which leaves Audio/RoonPlayer.cpp:227 - RoonCtrl::Instance(host, port), the
#call that carries the user's configuration across - uncovered. The review of
#T3.28b proved it bites: exchanging it for RoonCtrl::Instance(host,
#RoonArgs::DefaultPort) left core/RoonArgs_test at 14/14, exit 0. That is the
#field defect of T3.28 moved one call site further on.
#A test binary is a process: this one builds a REAL RoonPlayer with a non-empty
#zone_id and watches the launch that follows, so both forms are covered without
#a fork, a seam, or a test-only hook in src/.
#⚠️ ONE case on purpose. A second case needing a differently-configured
#controller in the same process would be back on the same singleton.
#Same linkage as core/RoonArgs_test: Audio/RoonPlayer.$(OBJEXT) on top of the
#core objects; -I.../Audio is already in AM_CPPFLAGS.
#⚠️ _DEPENDENCIES = libcalaos_common.la, like 49 of the test binaries, so
#automake does NOT relink this binary when RoonPlayer.o changes: any mutation
#campaign must rm -f BOTH the .o and the test binary, then check the CXXLD line
#really came back (double space, path relative to tests/ - match it with a
#regex, never a single space).
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/RoonSpawnViaPlayer_test
check_PROGRAMS += core/RoonSpawnViaPlayer_test
core_RoonSpawnViaPlayer_test_SOURCES = core/RoonSpawnViaPlayer_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h \
                  core/RoonSpawnHarness.h
core_RoonSpawnViaPlayer_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_RoonSpawnViaPlayer_test_LDADD = $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT) \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_RoonSpawnViaPlayer_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.25 (A)
#The parsing contract of Utils::from_string()/Utils::is_of_type()
#(src/lib/StringUtils.h). Everything under test is a template in a header of
#libcalaos_common, so nothing more is linked and there is no _DEPENDENCIES
#pitfall to dodge. AM_CPPFLAGS already carries -I$(top_srcdir)/src/lib.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += StringUtilsFromString_test
check_PROGRAMS += StringUtilsFromString_test
StringUtilsFromString_test_SOURCES = StringUtilsFromString_test.cpp
StringUtilsFromString_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
#Built elsewhere, do not let automake turn them into prerequisites
StringUtilsFromString_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.25 (B)
#The remotely reachable chain of T3.25: set_state -> decodeSetState ->
#OutputShutter::set_value()/OutputLightDimmer::set_value() -> from_string().
#Reuses the E4.0a harness (core/JsonApiCharacterization.*) UNMODIFIED, so the
#same link closure as the other JsonApi* core suites is required - the harness
#builds a reference house containing a StandardMjpeg camera and a RoonPlayer
#and pulls both transports in. NO GOLDEN is produced or read by this suite:
#every oracle is a direct assertion on cmd_state, which embeds
#to_string(impulse_action_time) character for character, so the 145 goldens of
#the tree are untouched. IO/OutputShutter and IO/OutputLightDimmer are NOT part
#of CORE_SERVER_OBJECTS (they appear only in core_KnxIo_test_LDADD), hence the
#two extra objects below. -DCALAOS_GOLDEN_DIR is still defined because the
#harness compiles its golden helpers unconditionally.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/SetStateGarbage_test
check_PROGRAMS += core/SetStateGarbage_test
core_SetStateGarbage_test_SOURCES = core/SetStateGarbage_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_SetStateGarbage_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_SetStateGarbage_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/HttpClient.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/HttpServer.$(OBJEXT)                       \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerHttp.$(OBJEXT)               \
                  $(CALAOS_SERVER_BUILDDIR)/JsonApiHandlerWS.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/McpProxyHandler.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/McpServerManager.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/PollListenner.$(OBJEXT)                    \
                  $(CALAOS_SERVER_BUILDDIR)/WebSocket.$(OBJEXT)                        \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightDimmer.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutter.$(OBJEXT)                 \
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
core_SetStateGarbage_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.25 (B2)
#The same defect with the API boundary bypassed: OutputShutter::set_value() and
#OutputLightDimmer::set_value() called directly, the way an in-process caller
#does. SEPARATE BINARY ON PURPOSE - on the characterization commit these cases
#take SIGSEGV (the indeterminate duration arms Timer::singleShot() on a
#negative delay), and a death inside core/SetStateGarbage_test hid six of its
#cases. Splitting bounds what a crash can hide. CoreFixture only, so no JSON
#API object and no libuv loop is ever run here; the link closure is
#CORE_TEST_LDADD plus the two IO objects, which are NOT in CORE_SERVER_OBJECTS.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/ImpulseGarbageIo_test
check_PROGRAMS += core/ImpulseGarbageIo_test
core_ImpulseGarbageIo_test_SOURCES = core/ImpulseGarbageIo_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_ImpulseGarbageIo_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_ImpulseGarbageIo_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightDimmer.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputShutter.$(OBJEXT)                 \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_ImpulseGarbageIo_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.25 (review reserve 1)
#The oracle the Wago `port` family did not have. WIAnalog/WITemp/WOAnalog/
#WODali/WODaliRVB/WODigital appeared in NO test file of the tree, so their nine
#from_string_or_keep("port") lines were pinned by nothing. The seam is
#WagoMap::get_maps(): WagoMap keys its singletons on the (host, port) pair and
#publishes the table, so the port an IO chose is observable without adding a
#single accessor to production. Every case uses its own TEST-NET-1 host
#(RFC 5737), which never binds and never collides with a parallel `make check`
#peer. None of the six Wago IO objects, nor WagoMap/WagoCtrl, nor the
#InputTemp/OutputAnalog/OutputLightRGB parents are part of CORE_SERVER_OBJECTS,
#hence the list below. ⚠️ WagoCtrl.$(OBJEXT) is deliberately NOT linked: WagoMap.cpp
#only #includes WagoCtrl.h and uses no symbol of it, while the object itself pulls
#in the whole libmbus C library.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/WagoPortDefault_test
check_PROGRAMS += core/WagoPortDefault_test
core_WagoPortDefault_test_SOURCES = core/WagoPortDefault_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_WagoPortDefault_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_WagoPortDefault_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/AnalogIO.$(OBJEXT)                      \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputAnalog.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputTemp.$(OBJEXT)                     \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputAnalog.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightDimmer.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightRGB.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WagoMap.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WIAnalog.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WITemp.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WOAnalog.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODali.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODaliRVB.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODigital.$(OBJEXT)                \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_WagoPortDefault_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# E4.1l
#The BYTES of an event, on the three wires CalaosEvent::toJson() travels: the
#websocket push, the HTTP poll_listen answer, and the HISTORY ROW that
#EventManager::appendEvent() dumps into HistEvent::event_raw. The 145 goldens of
#E4.0 compare parsed documents, so neither a key order nor an escaping change can
#make any of them red - every assertion here reads WsTestSession::lastMessage(),
#HttpTestRequest::body() and HistEvent::event_raw RAW, like E4.1b's
#core/JsonApiEmissionBytes_test does.
#The history wire had NO test at all before this file: CalaosCoreFixture.h says
#HistLogger is only reachable for an IO flagged log_history="true" and the
#reference house flags none, so the only production producer of event_raw was
#linked (EventManager.o is in CORE_SERVER_OBJECTS) but never executed.
#HistLogger is brought in exactly like core/JsonApiEventLog_test,
#core/JsonApiSession_test and core/JsonApiEmissionBytes_test do - built once,
#before the first CoreFixture::SetUp(), in a directory of its own (template
#calaos_e41l_hist_XXXXXX) so no two binaries ever share a database.
#Same link closure as core/JsonApiEmissionBytes_test: it drives both transports,
#needs PollListenner for poll_listen and HistLogger for the history row, both
#already part of CORE_SERVER_OBJECTS or listed below.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/EventWireBytes_test
check_PROGRAMS += core/EventWireBytes_test
core_EventWireBytes_test_SOURCES = core/EventWireBytes_test.cpp \
                  core/JsonApiCharacterization.cpp \
                  core/JsonApiCharacterization.h \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_EventWireBytes_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/RemoteUI \
                  -DCALAOS_GOLDEN_DIR='"$(srcdir)/core/golden"'
core_EventWireBytes_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IPCam/StandardMjpeg.$(OBJEXT)              \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/AudioDB.$(OBJEXT)                    \
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
core_EventWireBytes_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.46
#The WRITE half of the Wago REPLY path: SingleBit_cb and SingleWord_cb
#(WagoMap.h), the two slots that carry a write acknowledgement back from
#calaos_wago. SingleWord_cb carried two bare Utils::UWord in a row - address
#and value, interchangeable with no diagnostic, the pair F-WAGO-7 names,
#arriving instead of leaving. T3.31 typed the OUTBOUND half and left this one,
#named in T3.46.md section 6.
#These cases are a COMPILATION property reported through an executable oracle
#(std::is_invocable_v read by EXPECT_FALSE), NOT a behavioural oracle, and the
#file says so at length: not one of the four implementations of these two
#slots is reachable from a test binary. The closure of the implementations
#themselves is established by mutation, in T3.46.md section 7.
#It includes WagoMap.h and NOTHING else from the server, so no server object
#is linked and none may be - that keeps it clear of the _DEPENDENCIES pitfall.
#-I.../IO/Wago and -I.../IO are already in AM_CPPFLAGS.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += WagoWriteReply_test
check_PROGRAMS += WagoWriteReply_test
WagoWriteReply_test_SOURCES = WagoWriteReply_test.cpp
WagoWriteReply_test_CPPFLAGS = $(AM_CPPFLAGS) \
                  -DCALAOS_TOP_SRCDIR='"$(abs_top_srcdir)"'
WagoWriteReply_test_LDADD = @CALAOS_SERVER_LIBS@ \
                  $(top_builddir)/src/lib/libcalaos_common.la \
                  -lgtest -lgtest_main
endif

# T3.40
#⭐ A fire-and-forget one-shot must not outlive the IO that armed it.
#Timer::singleShot()/Idler::singleIdler() copy the slot into an ANONYMOUS uvw
#handle: no destructor and no member can cancel it, and sigc::trackable never
#disconnects a lambda. Every IO in the tree can die before the deadline
#(ListeRoom::deleteIO() from the JSON API, Room::RemoveIO(), ~Room() on a
#configuration reload), so the four sites T3.34 guarded were not the only ones.
#Oracle: each probe is built with placement new into a buffer the TEST owns,
#destroyed explicitly, and the buffer is then poisoned with 0xA5 - a value none
#of the callbacks writes. A write through the dangling `this` breaks the poison
#and a dereference kills the binary, so this suite is judged on the EXIT CODE
#first (a SIGSEGV prints no FAILED line) and on the log second.
#The suites are declared in a deliberate order and the file carries its own
#main() with _exit(): see the file header.
#Links the switch base classes, the KNX objects (KNXIo<Base> is a template
#mixin instantiated by the test itself, exactly as KNXIo.cpp does) and
#Audio/RoonPlayer.o on top of the core closure; IO/Scenario.o and
#Audio/AudioPlayer.o are already in CORE_SERVER_OBJECTS.
#⚠️ _DEPENDENCIES = libcalaos_common.la, like most test binaries here, so
#automake does NOT relink when one of those .o changes: a mutation campaign
#must rm -f BOTH the .o and the test binary, rebuild from the ROOT, and check
#the CXXLD line really came back (double space, path relative to tests/ -
#match it with a regex, never a single space).
#⭐ CALAOS_SRC_DIR is the SOURCE tree, read at run time by the three
#IoLifetimeSourceGuardTest cases: they census the raw
#Timer::singleShot()/Idler::singleIdler() call sites (a guard that is ABSENT on
#a NEW site is what LifetimeTag cannot make unwritable) and the places that
#pump the event loop (the invariant that closes the "the token dies last"
#window). abs_top_srcdir, not srcdir: the binary runs from tests/ and the tree
#may be a VPATH build.
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/IoLifetimeTimer_test
check_PROGRAMS += core/IoLifetimeTimer_test
core_IoLifetimeTimer_test_SOURCES = core/IoLifetimeTimer_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h \
                  core/RoonSpawnHarness.h
core_IoLifetimeTimer_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS) \
                  -I$(top_srcdir)/src/bin/calaos_server/IO/KNX \
                  -DCALAOS_SRC_DIR='"$(abs_top_srcdir)/src"'
core_IoLifetimeTimer_test_LDADD = $(CORE_TEST_LDADD) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/AnalogIO.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitch.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitchLongPress.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputSwitchTriple.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXBase.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/IO/KNX/KNXCtrl.$(OBJEXT) \
                  $(CALAOS_SERVER_BUILDDIR)/Audio/RoonPlayer.$(OBJEXT)
#Built elsewhere, do not let automake turn them into prerequisites
core_IoLifetimeTimer_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif

# T3.50
#The READ half of the Wago REPLY path: MultiBits_cb and MultiWords_cb
#(WagoMap.h:38-39), the two slots that carry a read reply back from
#calaos_wago. They carried (UWord address, int count) - two types that convert
#into one another in silence, the pair E4.1h measured GREEN on a swap. T3.46
#closed the WRITE half and named this one as the next ticket of the chain.
#The *Refuses* cases are a COMPILATION property reported through an executable
#oracle (std::is_invocable_v read by EXPECT_FALSE), never a behavioural one,
#and every EXPECT_FALSE has its EXPECT_TRUE control next to it.
#⭐ Unlike WagoWriteReply_test this one also probes the IMPLEMENTATIONS through
#pointers to member, and EXERCISES one of them for real - which is why it links
#the Wago server objects and lives under core/ with the fixture, exactly like
#core/WagoPortDefault_test whose LDADD list this copies.
#Built elsewhere: the server objects come from another Makefile, so only
#libcalaos_common.la may appear in _DEPENDENCIES (the -DEPENDENCIES pitfall).
#Own HAVE_GTEST block so this stays a pure append at the end of the file.
if HAVE_GTEST
TESTS += core/WagoReadReply_test
check_PROGRAMS += core/WagoReadReply_test
core_WagoReadReply_test_SOURCES = core/WagoReadReply_test.cpp \
                  core/CalaosCoreFixture.cpp \
                  core/CalaosCoreFixture.h
core_WagoReadReply_test_CPPFLAGS = $(CORE_TEST_CPPFLAGS)
core_WagoReadReply_test_LDADD = \
                  $(CALAOS_SERVER_BUILDDIR)/IO/AnalogIO.$(OBJEXT)                      \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputAnalog.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/InputTemp.$(OBJEXT)                     \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputAnalog.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLight.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightDimmer.$(OBJEXT)             \
                  $(CALAOS_SERVER_BUILDDIR)/IO/OutputLightRGB.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WagoMap.$(OBJEXT)                  \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WIAnalog.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WITemp.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WOAnalog.$(OBJEXT)                 \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODali.$(OBJEXT)                   \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODaliRVB.$(OBJEXT)                \
                  $(CALAOS_SERVER_BUILDDIR)/IO/Wago/WODigital.$(OBJEXT)                \
                  $(CORE_TEST_LDADD)
#Built elsewhere, do not let automake turn them into prerequisites
core_WagoReadReply_test_DEPENDENCIES = $(top_builddir)/src/lib/libcalaos_common.la
endif
