
MAINTAINERCLEANFILES = Makefile.in

AM_CFLAGS = \
-I. \
-I./Rules \
-I./IPCam \
-I./Audio \
-I./IO \
-I./IO/Wago \
-I./IO/Wago/libmbus \
-I./IO/OneWire \
-I./IO/X10 \
-I./IO/Scripts \
-I./IO/GCE \
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
-I./IO/Wago \
-I./IO/Wago/libmbus \
-I./IO/OneWire \
-I./IO/X10 \
-I./IO/Scripts \
-I./IO/GCE \
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
-I$(top_srcdir)/src/bin/calaos_server/IO/Wago\
-I$(top_srcdir)/src/bin/calaos_server/IO/OneWire\
-I$(top_srcdir)/src/bin/calaos_server/IO/X10\
-I$(top_srcdir)/src/bin/calaos_server/IO/Scripts\
-I$(top_srcdir)/src/bin/calaos_server/Audio\
-I$(top_srcdir)/src/bin/calaos_server/IPCam\
-I$(top_srcdir)/src/bin/calaos_server/TCPProcessor\
-I$(top_srcdir)/src/bin/calaos_server/IO/Wago/libmbus\
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
DataLogger.cpp 					\
Output.cpp					\
ListeRule.cpp					\
Config.cpp					\
IO/Wago/WagoCtrl.cpp					\
IO/Wago/WODigital.cpp				\
IO/Wago/WagoMap.cpp					\
IO/Wago/WIDigitalBP.cpp				\
IO/Wago/WIDigitalTriple.cpp				\
IO/Wago/WIDigitalLong.cpp				\
TCPServer.cpp					\
TCPConnection.cpp				\
Audio/AudioDB.cpp				\
Audio/AudioManager.cpp				\
Audio/Squeezebox.cpp				\
Audio/SqueezeboxDB.cpp				\
Audio/AudioInput.cpp				\
Audio/AudioOutput.cpp				\
IO/IntValue.cpp					\
IO/Wago/WITemp.cpp					\
IO/OneWire/OWTemp.cpp					\
IPCam/IPCam.cpp					\
IPCam/CamInput.cpp				\
IPCam/CamManager.cpp				\
IPCam/CamOutput.cpp				\
IO/Scenario.cpp					\
IPCam/Gadspot.cpp				\
IPCam/Axis.cpp					\
IPCam/StandardMjpeg.cpp				\
IO/Wago/WOVolet.cpp					\
UDPServer.cpp					\
IO/X10/X10Output.cpp				\
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
IO/Wago/WODali.cpp					\
IO/Wago/WODaliRVB.cpp				\
TCPProcessor/AudioCommand.cpp			\
Audio/AudioPlayer.cpp				\
IO/Wago/WOVoletSmart.cpp				\
IO/Wago/libmbus/mbus.c				\
IO/Wago/libmbus/mbus_cmd.c				\
IO/Wago/libmbus/mbus_rqst.c				\
IO/Wago/libmbus/mbus_sock.c				\
Scenario/AutoScenario.cpp			\
IO/Wago/WOAnalog.cpp					\
IO/Wago/WIAnalog.cpp					\
PollListenner.cpp				\
Audio/AVRManager.cpp				\
Audio/AVReceiver.cpp				\
Audio/AVRPioneer.cpp				\
Audio/AVRDenon.cpp				\
Audio/AVROnkyo.cpp				\
Audio/AVRMarantz.cpp				\
Audio/AVRYamaha.cpp				\
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
Audio/AVRYamaha.h				\
Audio/AudioInput.h				\
PollListenner.h					\
DataLogger.h					\
IOBase.h					\
Input.h						\
Rule.h						\
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
IO/Wago/WIDigitalBP.h				\
IO/Wago/WODali.h					\
IO/Wago/WIAnalog.h					\
IO/InPlageHoraire.h				\
IO/Wago/WagoCtrl.h					\
IO/InputTime.h					\
IO/OneWire/OWTemp.h					\
IO/Wago/WODigital.h					\
IO/Wago/WITemp.h					\
IO/Scenario.h					\
IO/X10/X10Output.h					\
IO/InputTimer.h					\
IO/Wago/libmbus/mbus.h				\
IO/Wago/libmbus/mbus_conf.h				\
IO/Wago/WOVoletSmart.h				\
IO/Wago/WagoMap.h					\
IO/IntValue.h					\
IO/Wago/WOAnalog.h					\
IO/IOFactory.h					\
IO/Wago/WODaliRVB.h					\
IO/Wago/WIDigitalTriple.h				\
IO/Wago/WIDigitalLong.h				\
IO/Wago/WOVolet.h					\
IO/OutputFake.h					\
TCPServer.h						\
IO/OutputLight.h				\
IO/OutputLight.cpp				\
IO/OutputLightDimmer.h			\
IO/OutputLightDimmer.cpp		\
IO/OutputLightRGB.h				\
IO/OutputLightRGB.cpp			\
IO/InputSwitch.h				\
IO/InputSwitch.cpp				\
IO/InputSwitchTriple.h			\
IO/InputSwitchTriple.cpp		\
IIO/InputSwitchLongPress.h		\
IO/InputSwitchLongPress.cpp		\
O/InputTemp.h					\
IO/InputTemp.cpp				\
IO/InputAnalog.h				\
IO/InputAnalog.cpp				\
IO/OutputAnalog.h				\
IO/OutputAnalog.cpp				\
IO/OutputShutter.h				\
IO/OutputShutter.cpp			\
IO/OutputShutterSmart.h			\
IO/OutputShutterSmart.cpp

calaos_server_LDADD = \
@CALAOS_SERVER_LIBS@ \
$(top_builddir)/src/lib/libcalaos_common.la
