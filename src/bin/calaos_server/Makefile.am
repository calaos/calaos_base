
MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = \
-I. \
-I./Rules \
-I./IPCam \
-I./Audio \
-I./IO \
-I./IO/libmbus \
-I./Scenario \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/calaos\" \
-I$(top_srcdir)/src/lib \
-I$(top_srcdir)/src/lib/LuaScript \
-I$(top_srcdir)/src/bin/calaos_server/\
-I$(top_srcdir)/src/bin/calaos_server/Rules\
@CALAOS_COMMON_CFLAGS@ \
@CALAOS_SERVER_CFLAGS@ \
-DLIBMBUS

AM_CXXFLAGS = \
-I. \
-I./Rules \
-I./IPCam \
-I./Audio \
-I./IO \
-I./IO/libmbus \
-I./Scenario \
-DPACKAGE_BIN_DIR=\"$(bindir)\" \
-DPACKAGE_LIB_DIR=\"$(libdir)\" \
-DPACKAGE_DATA_DIR=\"$(datadir)/calaos\" \
-DETC_DIR=\"$(sysconfdir)/calaos/\" \
-I$(top_srcdir)/src/lib \
-I$(top_srcdir)/src/lib/LuaScript \
-I$(top_srcdir)/src/bin/calaos_server\
-I$(top_srcdir)/src/bin/calaos_server/Rules\
-I$(top_srcdir)/src/bin/calaos_server/IO\
-I$(top_srcdir)/src/bin/calaos_server/Audio\
-I$(top_srcdir)/src/bin/calaos_server/IPCam\
-I$(top_srcdir)/src/bin/calaos_server/TCPProcessor\
-I$(top_srcdir)/src/bin/calaos_server/IO/libmbus\
-I$(top_srcdir)/src/bin/calaos_server/Scenario\
@CALAOS_COMMON_CFLAGS@ \
@CALAOS_SERVER_CFLAGS@ \
-DLIBMBUS


bin_PROGRAMS = calaos_server

calaos_server_SOURCES = \
main.cpp					\
Calaos.cpp					\
Room.cpp					\
IO/InputTime.cpp				\
IO/InputTimer.cpp				\
IO/OutputFake.cpp				\
IO/IOFactory.cpp				\
ListeRoom.cpp					\
Rules/Action.cpp				\
Rules/ActionStd.cpp				\
Rules/ActionTouchscreen.cpp			\
Rules/ActionMail.cpp				\
Rules/ActionScript.cpp				\
Rules/Condition.cpp				\
Rules/ConditionStd.cpp				\
Rules/ConditionOutput.cpp			\
Rules/ConditionStart.cpp			\
Rules/ConditionScript.cpp			\
Rules/RulesFactory.cpp				\
Rule.cpp					\
Output.cpp					\
ListeRule.cpp					\
Config.cpp					\
IO/WagoCtrl.cpp					\
IO/WODigital.cpp				\
IO/WagoMap.cpp					\
IO/WIDigitalBP.cpp				\
IO/WIDigitalTriple.cpp				\
TCPServer.cpp					\
TCPConnection.cpp				\
Audio/AudioDB.cpp				\
Audio/AudioManager.cpp				\
Audio/Squeezebox.cpp				\
Audio/SqueezeboxDB.cpp				\
Audio/AudioInput.cpp				\
Audio/AudioOutput.cpp				\
IO/IntValue.cpp					\
IO/WITemp.cpp					\
IO/OWTemp.cpp					\
IPCam/IPCam.cpp					\
IPCam/CamInput.cpp				\
IPCam/CamManager.cpp				\
IPCam/CamOutput.cpp				\
IO/Scenario.cpp					\
IPCam/Gadspot.cpp				\
IPCam/Axis.cpp					\
IPCam/StandardMjpeg.cpp				\
IO/WOVolet.cpp					\
UDPServer.cpp					\
IO/X10Output.cpp				\
IO/InPlageHoraire.cpp				\
Input.cpp					\
IPCam/Planet.cpp				\
TCPProcessor/BaseCommand.cpp			\
TCPProcessor/CameraCommand.cpp			\
TCPProcessor/IOCommand.cpp			\
TCPProcessor/HomeCommand.cpp			\
TCPProcessor/RulesCommand.cpp			\
TCPProcessor/ListenCommand.cpp			\
TCPProcessor/ScenarioCommand.cpp		\
IPCam/CamConnection.cpp				\
IPCam/CamServer.cpp				\
IO/WODali.cpp					\
IO/WODaliRVB.cpp				\
TCPProcessor/AudioCommand.cpp			\
Audio/AudioPlayer.cpp				\
IO/WOVoletSmart.cpp				\
IO/libmbus/mbus.c				\
IO/libmbus/mbus_cmd.c				\
IO/libmbus/mbus_rqst.c				\
IO/libmbus/mbus_sock.c				\
Scenario/AutoScenario.cpp			\
IO/WOAnalog.cpp					\
IO/WIAnalog.cpp					\
PollListenner.cpp				\
Audio/AVRManager.cpp				\
Audio/AVReceiver.cpp				\
Audio/AVRPioneer.cpp				\
Audio/AVRDenon.cpp				\
Audio/AVROnkyo.cpp				\
Audio/AVRMarantz.cpp				\
IPCam/Planet.h					\
IPCam/Axis.h					\
IPCam/StandardMjpeg.h				\
IPCam/CamOutput.h				\
IPCam/CamServer.h				\
IPCam/Gadspot.h					\
IPCam/CamManager.h				\
IPCam/IPCam.h					\
IPCam/CamConnection.h				\
IPCam/CamInput.h				\
UDPServer.h					\
Scenario/ScenarioStep.h				\
Scenario/ScenarioIO.h				\
Scenario/Scenarios.h				\
Scenario/Scenario.h				\
Scenario/AutoScenario.h				\
Audio/AudioPlayerData.h				\
Audio/AVRDenon.h				\
Audio/AudioPlayer.h				\
Audio/AudioManager.h				\
Audio/AVRManager.h				\
Audio/AudioDB.h					\
Audio/AVRPioneer.h				\
Audio/SqueezeboxDB.h				\
Audio/Squeezebox.h				\
Audio/AVROnkyo.h				\
Audio/AudioOutput.h				\
Audio/AVRMarantz.h				\
Audio/AVReceiver.h				\
Audio/AudioInput.h				\
PollListenner.h					\
IOBase.h					\
Input.h						\
Etage.h						\
Rule.h						\
WagoHeartbeat.h					\
TCPConnection.h					\
Calaos.h					\
ListeRoom.h					\
Output.h					\
Config.h					\
Rules/RulesFactory.h				\
Rules/ActionTouchscreen.h			\
Rules/ConditionStd.h				\
Rules/Condition.h				\
Rules/ConditionOutput.h				\
Rules/ConditionStart.h				\
Rules/Action.h					\
Rules/ActionStd.h				\
Rules/ConditionScript.h				\
Rules/ActionMail.h				\
Rules/ActionScript.h				\
Room.h						\
ListeRule.h					\
IO/WIDigitalBP.h				\
IO/WODali.h					\
IO/WIAnalog.h					\
IO/InPlageHoraire.h				\
IO/WagoCtrl.h					\
IO/InputTime.h					\
IO/OWTemp.h					\
IO/WODigital.h					\
IO/WITemp.h					\
IO/Scenario.h					\
IO/X10Output.h					\
IO/InputTimer.h					\
IO/libmbus/mbus.h				\
IO/libmbus/mbus_conf.h				\
IO/WOVoletSmart.h				\
IO/WagoMap.h					\
IO/IntValue.h					\
IO/WOAnalog.h					\
IO/IOFactory.h					\
IO/WODaliRVB.h					\
IO/WIDigitalTriple.h				\
IO/WOVolet.h					\
IO/OutputFake.h					\
TCPServer.h					\
IO/sunset.c               

calaos_server_LDADD = \
@CALAOS_SERVER_LIBS@ \
$(top_builddir)/src/lib/libcalaos_common.la
