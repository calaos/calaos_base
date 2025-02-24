MAINTAINERCLEANFILES = Makefile.in

AM_CPPFLAGS =                                                 \
        -DETC_DIR=\"$(sysconfdir)/calaos/\"                   \
        -DLIBMBUS                                             \
        -DPACKAGE_BIN_DIR=\"$(bindir)\"                       \
        -DPACKAGE_DATA_DIR=\"$(datadir)/calaos\"              \
        -DPACKAGE_DATA_DIR=\"$(pkgdatadir)\"                  \
        -DPACKAGE_LIB_DIR=\"$(libdir)\"                       \
        -DPACKAGE_LOCALE_DIR=\"$(datadir)/locale\"            \
        -DTIXML_USE_STL                                       \
        -DLUA_USE_DLOPEN                                      \
        -DXPLLIB_NOSOCK                                       \
        -DXPLLIB_NOLOG                                        \
        -DXPLLIB_NOCONF                                       \
        -I$(top_srcdir)/src/bin/calaos_server/Audio           \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Gpio         \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Mqtt         \
        -I$(top_srcdir)/src/bin/calaos_server/IO/OLA          \
        -I$(top_srcdir)/src/bin/calaos_server/IO/OneWire      \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Ping         \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Scripts      \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Teleinfo     \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Wago/libmbus \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Wago         \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Web          \
        -I$(top_srcdir)/src/bin/calaos_server/IO/X10          \
        -I$(top_srcdir)/src/bin/calaos_server/IO/xPL          \
        -I$(top_srcdir)/src/bin/calaos_server/IO/xPL/xPLLib   \
        -I$(top_srcdir)/src/bin/calaos_server/IO/Zibase       \
        -I$(top_srcdir)/src/bin/calaos_server/IO              \
        -I$(top_srcdir)/src/bin/calaos_server/IPCam           \
        -I$(top_srcdir)/src/bin/calaos_server/LuaScript       \
        -I$(top_srcdir)/src/bin/calaos_server/Rules           \
        -I$(top_srcdir)/src/bin/calaos_server/Scenario        \
        -I$(top_srcdir)/src/bin/calaos_server                 \
        -I$(top_srcdir)/src/lib                               \
        -I$(top_srcdir)/src/lib/LuaScript                     \
        -I$(top_srcdir)/src/lib/TinyXML                       \
        -I$(top_srcdir)/src/lib/cJSON                         \
        -I$(top_srcdir)/src/lib/llhttp/include                \
        -I$(top_srcdir)/src/lib/libquickmail                  \
        -I$(top_srcdir)/src/lib/uri_parser                    \
        -I$(top_srcdir)/src/lib/sqlite_modern_cpp/hdr         \
        -I$(top_srcdir)/src/lib/sole                          \
        @CALAOS_COMMON_CFLAGS@                                \
        @CALAOS_SERVER_CFLAGS@                                \
        @LIBOLA_CFLAGS@                                       \
        @PTHREAD_CFLAGS@

bin_PROGRAMS = calaos_server

calaos_server_SOURCES = \
        Audio/AVRDenon.cpp                              \
        Audio/AVRDenon.h                                \
        Audio/AVRManager.cpp                            \
        Audio/AVRManager.h                              \
        Audio/AVRMarantz.cpp                            \
        Audio/AVRMarantz.h                              \
        Audio/AVROnkyo.cpp                              \
        Audio/AVROnkyo.h                                \
        Audio/AVRPioneer.cpp                            \
        Audio/AVRPioneer.h                              \
        Audio/AVRYamaha.cpp                             \
        Audio/AVRYamaha.h                               \
        Audio/AVRRose.cpp                               \
        Audio/AVRRose.h                                 \
        Audio/AVReceiver.cpp                            \
        Audio/AVReceiver.h                              \
        Audio/AudioDB.cpp                               \
        Audio/AudioDB.h                                 \
        Audio/AudioPlayer.cpp                           \
        Audio/AudioPlayer.h                             \
        Audio/AudioPlayerData.h                         \
        Audio/Squeezebox.cpp                            \
        Audio/Squeezebox.h                              \
        Audio/SqueezeboxDB.cpp                          \
        Audio/SqueezeboxDB.h                            \
        Audio/RoonPlayer.cpp                            \
        Audio/RoonPlayer.h                              \
        Calaos.cpp                                      \
        Calaos.h                                        \
        CalaosConfig.cpp                                \
        CalaosConfig.h                                  \
        DataLogger.cpp                                  \
        DataLogger.h                                    \
        EventManager.cpp                                \
        EventManager.h                                  \
        HttpClient.cpp                                  \
        HttpClient.h                                    \
        HttpCodes.h                                     \
        HttpServer.cpp                                  \
        HttpServer.h                                    \
        IO/AnalogIO.cpp                                 \
        IO/AnalogIO.h                                   \
        IO/ExternProc.cpp                               \
        IO/ExternProc.h                                 \
        IO/Gpio/GpioCtrl.cpp                            \
        IO/Gpio/GpioCtrl.h                              \
        IO/Gpio/GpioInputSwitch.cpp                     \
        IO/Gpio/GpioInputSwitch.h                       \
        IO/Gpio/GpioInputSwitchLongPress.cpp            \
        IO/Gpio/GpioInputSwitchLongPress.h              \
        IO/Gpio/GpioInputSwitchTriple.cpp               \
        IO/Gpio/GpioInputSwitchTriple.h                 \
        IO/Gpio/GpioOutputShutter.cpp                   \
        IO/Gpio/GpioOutputShutter.h                     \
        IO/Gpio/GpioOutputShutterSmart.cpp              \
        IO/Gpio/GpioOutputShutterSmart.h                \
        IO/Gpio/GpioOutputSwitch.cpp                    \
        IO/Gpio/GpioOutputSwitch.h                      \
        IO/IODoc.cpp                                    \
        IO/IODoc.h                                      \
        IO/IOFactory.cpp                                \
        IO/IOFactory.h                                  \
        IO/InPlageHoraire.cpp                           \
        IO/InPlageHoraire.h                             \
        IO/InputAnalog.cpp                              \
        IO/InputAnalog.h                                \
        IO/InputString.cpp                              \
        IO/InputString.h                                \
        IO/InputSwitch.cpp                              \
        IO/InputSwitch.h                                \
        IO/InputSwitchLongPress.cpp                     \
        IO/InputSwitchLongPress.h                       \
        IO/InputSwitchTriple.cpp                        \
        IO/InputSwitchTriple.h                          \
        IO/InputTemp.cpp                                \
        IO/InputTemp.h                                  \
        IO/InputTime.cpp                                \
        IO/InputTime.h                                  \
        IO/InputTimer.cpp                               \
        IO/InputTimer.h                                 \
        IO/IntValue.cpp                                 \
        IO/IntValue.h                                   \
        IO/Hue/HueOutputLightRGB.h                      \
        IO/Hue/HueOutputLightRGB.cpp                    \
        IO/KNX/KNXCtrl.h                                \
        IO/KNX/KNXCtrl.cpp                              \
        IO/KNX/KNXBase.cpp                              \
        IO/KNX/KNXBase.h                                \
        IO/KNX/KNXInputAnalog.h                         \
        IO/KNX/KNXInputAnalog.cpp                       \
        IO/KNX/KNXInputSwitch.h                         \
        IO/KNX/KNXInputSwitch.cpp                       \
        IO/KNX/KNXInputSwitchLongPress.h                \
        IO/KNX/KNXInputSwitchLongPress.cpp              \
        IO/KNX/KNXInputSwitchTriple.h                   \
        IO/KNX/KNXInputSwitchTriple.cpp                 \
        IO/KNX/KNXInputTemp.h                           \
        IO/KNX/KNXInputTemp.cpp                         \
        IO/KNX/KNXOutputAnalog.h                        \
        IO/KNX/KNXOutputAnalog.cpp                      \
        IO/KNX/KNXOutputLight.h                         \
        IO/KNX/KNXOutputLight.cpp                       \
        IO/KNX/KNXOutputLightDimmer.h                   \
        IO/KNX/KNXOutputLightDimmer.cpp                 \
        IO/KNX/KNXOutputLightRGB.h                      \
        IO/KNX/KNXOutputLightRGB.cpp                    \
        IO/KNX/KNXOutputShutter.h                       \
        IO/KNX/KNXOutputShutter.cpp                     \
        IO/KNX/KNXOutputShutterSmart.h                  \
        IO/KNX/KNXOutputShutterSmart.cpp                \
        IO/LAN/PingInputSwitch.cpp                      \
        IO/LAN/PingInputSwitch.h                        \
        IO/LAN/WOLOutputBool.cpp                        \
        IO/LAN/WOLOutputBool.h                          \
        IO/LimitlessLED/Milight.h                       \
        IO/LimitlessLED/Milight.cpp                     \
        IO/LimitlessLED/MilightOutputLightRGB.h         \
        IO/LimitlessLED/MilightOutputLightRGB.cpp       \
        IO/Mqtt/MqttBrokersList.h                        \
        IO/Mqtt/MqttBrokerslist.cpp                        \
        IO/Mqtt/MqttCtrl.h                                \
        IO/Mqtt/MqttCtrl.cpp                                \
        IO/Mqtt/MqttInputAnalog.h                        \
        IO/Mqtt/MqttInputAnalog.cpp                        \
        IO/Mqtt/MqttInputString.h                        \
        IO/Mqtt/MqttInputString.cpp                        \
        IO/Mqtt/MqttInputSwitch.h                        \
        IO/Mqtt/MqttInputSwitch.cpp                        \
        IO/Mqtt/MqttInputTemp.h                                \
        IO/Mqtt/MqttInputTemp.cpp                        \
        IO/Mqtt/MqttOutputAnalog.h                        \
        IO/Mqtt/MqttOutputAnalog.cpp                        \
        IO/Mqtt/MqttOutputLight.h                        \
        IO/Mqtt/MqttOutputLight.cpp                        \
        IO/Mqtt/MqttOutputLightDimmer.h                        \
        IO/Mqtt/MqttOutputLightDimmer.cpp                \
        IO/Mqtt/MqttOutputLightRGB.h                        \
        IO/Mqtt/MqttOutputLightRGB.cpp                  \
        IO/MySensors/MySensors.h                        \
        IO/MySensors/MySensors.cpp                      \
        IO/MySensors/MySensorsControllerList.cpp        \
        IO/MySensors/MySensorsControllerList.h          \
        IO/MySensors/MySensorsController.cpp            \
        IO/MySensors/MySensorsController.h              \
        IO/MySensors/MySensorsInputAnalog.h             \
        IO/MySensors/MySensorsInputAnalog.cpp           \
        IO/MySensors/MySensorsOutputLight.h             \
        IO/MySensors/MySensorsOutputLight.cpp           \
        IO/MySensors/MySensorsInputString.h             \
        IO/MySensors/MySensorsInputString.cpp           \
        IO/MySensors/MySensorsOutputString.h            \
        IO/MySensors/MySensorsOutputString.cpp          \
        IO/MySensors/MySensorsInputSwitch.h             \
        IO/MySensors/MySensorsInputSwitch.cpp           \
        IO/MySensors/MySensorsInputSwitchLongPress.h    \
        IO/MySensors/MySensorsInputSwitchLongPress.cpp  \
        IO/MySensors/MySensorsInputSwitchTriple.h       \
        IO/MySensors/MySensorsInputSwitchTriple.cpp     \
        IO/MySensors/MySensorsInputTemp.h               \
        IO/MySensors/MySensorsInputTemp.cpp             \
        IO/MySensors/MySensorsOutputAnalog.h            \
        IO/MySensors/MySensorsOutputAnalog.cpp          \
        IO/MySensors/MySensorsOutputDimmer.h            \
        IO/MySensors/MySensorsOutputDimmer.cpp          \
        IO/MySensors/MySensorsOutputLightRGB.h          \
        IO/MySensors/MySensorsOutputLightRGB.cpp        \
        IO/MySensors/MySensorsOutputShutter.h           \
        IO/MySensors/MySensorsOutputShutter.cpp         \
        IO/MySensors/MySensorsOutputShutterSmart.h      \
        IO/MySensors/MySensorsOutputShutterSmart.cpp    \
        IO/OLA/OLACtrl.cpp                              \
        IO/OLA/OLACtrl.h                                \
        IO/OLA/OLAOutputLightDimmer.cpp                 \
        IO/OLA/OLAOutputLightDimmer.h                   \
        IO/OLA/OLAOutputLightRGB.cpp                    \
        IO/OLA/OLAOutputLightRGB.h                      \
        IO/OneWire/OWCtrl.cpp                           \
        IO/OneWire/OWCtrl.h                             \
        IO/OneWire/OWTemp.cpp                           \
        IO/OneWire/OWTemp.h                             \
        IO/OutputAnalog.cpp                             \
        IO/OutputAnalog.h                               \
        IO/OutputFake.cpp                               \
        IO/OutputFake.h                                 \
        IO/OutputLight.cpp                              \
        IO/OutputLight.h                                \
        IO/OutputLightDimmer.cpp                        \
        IO/OutputLightDimmer.h                          \
        IO/OutputLightRGB.cpp                           \
        IO/OutputLightRGB.h                             \
        IO/OutputShutter.cpp                            \
        IO/OutputShutter.h                              \
        IO/OutputShutterSmart.cpp                       \
        IO/OutputShutterSmart.h                         \
        IO/OutputString.cpp                             \
        IO/OutputString.h                               \
        IO/Scenario.cpp                                 \
        IO/Scenario.h                                   \
        IO/Teleinfo/TeleinfoCtrl.cpp                    \
        IO/Teleinfo/TeleinfoCtrl.h                      \
        IO/Teleinfo/TeleinfoInputAnalog.cpp             \
        IO/Teleinfo/TeleinfoInputAnalog.h               \
        IO/Wago/WIAnalog.cpp                            \
        IO/Wago/WIAnalog.h                              \
        IO/Wago/WIDigitalBP.cpp                         \
        IO/Wago/WIDigitalBP.h                           \
        IO/Wago/WIDigitalLong.cpp                       \
        IO/Wago/WIDigitalLong.h                         \
        IO/Wago/WIDigitalTriple.cpp                     \
        IO/Wago/WIDigitalTriple.h                       \
        IO/Wago/WITemp.cpp                              \
        IO/Wago/WITemp.h                                \
        IO/Wago/WOAnalog.cpp                            \
        IO/Wago/WOAnalog.h                              \
        IO/Wago/WODali.cpp                              \
        IO/Wago/WODali.h                                \
        IO/Wago/WODaliRVB.cpp                           \
        IO/Wago/WODaliRVB.h                             \
        IO/Wago/WODigital.cpp                           \
        IO/Wago/WODigital.h                             \
        IO/Wago/WOVolet.cpp                             \
        IO/Wago/WOVolet.h                               \
        IO/Wago/WOVoletSmart.cpp                        \
        IO/Wago/WOVoletSmart.h                          \
        IO/Wago/WagoCtrl.cpp                            \
        IO/Wago/WagoCtrl.h                              \
        IO/Wago/WagoMap.cpp                             \
        IO/Wago/WagoMap.h                               \
        IO/Wago/libmbus/mbus.c                          \
        IO/Wago/libmbus/mbus.h                          \
        IO/Wago/libmbus/mbus_cmd.c                      \
        IO/Wago/libmbus/mbus_conf.h                     \
        IO/Wago/libmbus/mbus_rqst.c                     \
        IO/Wago/libmbus/mbus_sock.c                     \
        IO/Web/WebCtrl.cpp                              \
        IO/Web/WebCtrl.h                                \
        IO/Web/WebInputAnalog.cpp                       \
        IO/Web/WebInputAnalog.h                         \
        IO/Web/WebInputString.cpp                       \
        IO/Web/WebInputString.h                         \
        IO/Web/WebInputTemp.cpp                         \
        IO/Web/WebInputTemp.h                           \
        IO/Web/WebOutputLight.cpp                       \
        IO/Web/WebOutputLight.h                         \
        IO/Web/WebOutputLightRGB.cpp                    \
        IO/Web/WebOutputLightRGB.h                      \
        IO/Web/WebOutputString.cpp                      \
        IO/Web/WebOutputString.h                        \
        IO/Web/WebOutputAnalog.h                        \
        IO/Web/WebOutputAnalog.cpp                      \
        IO/Web/WebDocBase.h                             \
        IO/Web/WebDocBase.cpp                           \
        IO/X10/X10Output.cpp                            \
        IO/X10/X10Output.h                              \
        IO/xPL/xPLDocBase.cpp                           \
        IO/xPL/xPLDocBase.h                             \
        IO/xPL/xPLController.cpp                        \
        IO/xPL/xPLController.h                          \
        IO/xPL/xPLInputAnalog.cpp                       \
        IO/xPL/xPLInputAnalog.h                         \
        IO/xPL/xPLInputString.cpp                       \
        IO/xPL/xPLInputString.h                         \
        IO/xPL/xPLInputSwitch.cpp                       \
        IO/xPL/xPLInputSwitch.h                         \
        IO/xPL/xPLInputTemp.cpp                         \
        IO/xPL/xPLInputTemp.h                           \
        IO/xPL/xPLOutputAnalog.cpp                      \
        IO/xPL/xPLOutputAnalog.h                        \
        IO/xPL/xPLOutputString.cpp                      \
        IO/xPL/xPLOutputString.h                        \
        IO/xPL/xPLOutputSwitch.cpp                      \
        IO/xPL/xPLOutputSwitch.h                        \
        IO/xPL/xPLLib/Address.cpp                       \
        IO/xPL/xPLLib/Address.h                         \
        IO/xPL/xPLLib/xPLDevice.cpp                     \
        IO/xPL/xPLLib/xPLDevice.h                       \
        IO/xPL/xPLLib/Schemas/SchemaObject.cpp          \
        IO/xPL/xPLLib/Schemas/SchemaObject.h            \
        IO/xPL/xPLLib/Schemas/SchemaHbeat.cpp           \
        IO/xPL/xPLLib/Schemas/SchemaHbeat.h             \
        IO/xPL/xPLLib/Schemas/SchemaConfig.cpp          \
        IO/xPL/xPLLib/Schemas/SchemaConfig.h            \
        IO/xPL/xPLLib/Schemas/SchemaSensor.cpp          \
        IO/xPL/xPLLib/Schemas/SchemaSensor.h            \
        IO/xPL/xPLLib/Schemas/SchemaControl.cpp         \
        IO/xPL/xPLLib/Schemas/SchemaControl.h           \
        IO/xPL/xPLLib/StringTools/StringTools.cpp       \
        IO/xPL/xPLLib/StringTools/StringTools.h         \
        IO/Zibase/Zibase.cpp                            \
        IO/Zibase/Zibase.h                              \
        IO/Zibase/ZibaseTemp.cpp                        \
        IO/Zibase/ZibaseTemp.h                          \
        IO/Zibase/ZibaseAnalogIn.cpp                    \
        IO/Zibase/ZibaseAnalogIn.h                      \
        IO/Zibase/ZibaseDigitalIn.cpp                   \
        IO/Zibase/ZibaseDigitalIn.h                     \
        IO/Zibase/ZibaseDigitalOut.cpp                  \
        IO/Zibase/ZibaseDigitalOut.h                    \
        IOBase.cpp                                      \
        IOBase.h                                        \
        IPCam/Axis.cpp                                  \
        IPCam/Axis.h                                    \
        IPCam/Foscam.cpp                                \
        IPCam/Foscam.h                                  \
        IPCam/Gadspot.cpp                               \
        IPCam/Gadspot.h                                 \
        IPCam/IPCam.cpp                                 \
        IPCam/IPCam.h                                   \
        IPCam/Planet.cpp                                \
        IPCam/Planet.h                                  \
        IPCam/StandardMjpeg.cpp                         \
        IPCam/StandardMjpeg.h                           \
        IPCam/SynoSurveillanceStation.cpp               \
        IPCam/SynoSurveillanceStation.h                 \
        JsonApi.cpp                                     \
        JsonApi.h                                       \
        JsonApiHandlerHttp.h                            \
        JsonApiHandlerHttp.cpp                          \
        JsonApiHandlerWS.h                              \
        JsonApiHandlerWS.cpp                            \
        ListeRoom.cpp                                   \
        ListeRoom.h                                     \
        ListeRule.cpp                                   \
        ListeRule.h                                     \
        LuaScript/Lunar.h                               \
        LuaScript/ScriptExec.h                          \
        LuaScript/ScriptExec.cpp                        \
        PollListenner.cpp                               \
        PollListenner.h                                 \
        Room.cpp                                        \
        Room.h                                          \
        Rule.cpp                                        \
        Rule.h                                          \
        Rules/Action.cpp                                \
        Rules/Action.h                                  \
        Rules/ActionMail.cpp                            \
        Rules/ActionMail.h                              \
        Rules/ActionScript.cpp                          \
        Rules/ActionScript.h                            \
        Rules/ActionStd.cpp                             \
        Rules/ActionStd.h                               \
        Rules/ActionTouchscreen.cpp                     \
        Rules/ActionTouchscreen.h                       \
        Rules/ActionPush.h                              \
        Rules/ActionPush.cpp                            \
        Rules/Condition.cpp                             \
        Rules/Condition.h                               \
        Rules/ConditionOutput.cpp                       \
        Rules/ConditionOutput.h                         \
        Rules/ConditionScript.cpp                       \
        Rules/ConditionScript.h                         \
        Rules/ConditionStart.cpp                        \
        Rules/ConditionStart.h                          \
        Rules/ConditionStd.cpp                          \
        Rules/ConditionStd.h                            \
        Rules/RulesFactory.cpp                          \
        Rules/RulesFactory.h                            \
        Scenario/AutoScenario.cpp                       \
        Scenario/AutoScenario.h                         \
        UDPServer.cpp                                   \
        UDPServer.h                                     \
        WebSocket.cpp                                   \
        WebSocket.h                                     \
        HistLogger.cpp                                  \
        HistLogger.h                                    \
        main.cpp

calaos_server_LDADD =                                   \
        @CALAOS_COMMON_LIBS@                            \
        @CALAOS_SERVER_LIBS@                            \
        @INTLLIBS@                                      \
        @PTHREAD_CFLAGS@                                \
        $(top_builddir)/src/lib/libcalaos_common.la

calaos_server_LDFLAGS = -rdynamic

bin_PROGRAMS += calaos_script
calaos_script_SOURCES = \
        LuaScript/ScriptExtern_main.cpp \
        LuaScript/ScriptManager.h \
        LuaScript/ScriptManager.cpp \
        LuaScript/ScriptBindings.h \
        LuaScript/ScriptBindings.cpp \
        IO/ExternProc.cpp \
        IO/ExternProc.h

 calaos_script_LDADD = \
        @CALAOS_SERVER_LIBS@ \
        @INTLLIBS@ \
        $(top_builddir)/src/lib/libcalaos_common.la

bin_PROGRAMS += calaos_wago
calaos_wago_SOURCES = \
        IO/ExternProc.cpp                   \
        IO/ExternProc.h                     \
        IO/Wago/WagoCtrl.cpp                \
        IO/Wago/libmbus/mbus.c              \
        IO/Wago/libmbus/mbus.h              \
        IO/Wago/libmbus/mbus_cmd.c          \
        IO/Wago/libmbus/mbus_conf.h         \
        IO/Wago/libmbus/mbus_rqst.c         \
        IO/Wago/libmbus/mbus_sock.c         \
        IO/Wago/WagoExternProc_main.cpp

calaos_wago_LDADD = \
        @CALAOS_SERVER_LIBS@ \
        $(top_builddir)/src/lib/libcalaos_common.la

bin_PROGRAMS += calaos_1wire
calaos_1wire_SOURCES = \
        IO/ExternProc.cpp               \
        IO/ExternProc.h                 \
        IO/OneWire/OWExternProc_main.cpp

calaos_1wire_LDADD = \
        @CALAOS_SERVER_LIBS@ \
        $(top_builddir)/src/lib/libcalaos_common.la

if HAVE_LIBOLA
bin_PROGRAMS += calaos_ola
calaos_ola_SOURCES = \
        IO/ExternProc.cpp               \
        IO/ExternProc.h                 \
        IO/OLA/OLAExternProc_main.cpp

calaos_ola_LDADD = \
        @CALAOS_SERVER_LIBS@ \
        @LIBOLA_LIBS@ \
        $(top_builddir)/src/lib/libcalaos_common.la
endif

if HAVE_LIBKNX
bin_PROGRAMS += calaos_knx
calaos_knx_SOURCES = \
IO/ExternProc.cpp               \
IO/ExternProc.h                 \
IO/KNX/KNXExternProc_main.h     \
IO/KNX/KNXExternProc_main.cpp   \
IO/KNX/KNXExternProc_cli.cpp

calaos_knx_LDADD = \
@CALAOS_SERVER_LIBS@ \
-leibclient \
$(top_builddir)/src/lib/libcalaos_common.la
endif

if HAVE_LIBMOSQUITTO
bin_PROGRAMS += calaos_mqtt
calaos_mqtt_SOURCES = \
IO/ExternProc.cpp               \
IO/ExternProc.h                 \
IO/Mqtt/MqttExternProc_main.h   \
IO/Mqtt/MqttExternProc_main.cpp

calaos_mqtt_LDADD = \
@CALAOS_SERVER_LIBS@ \
-lmosquitto -lmosquittopp \
$(top_builddir)/src/lib/libcalaos_common.la
endif

if HAVE_PYTHON_ROON
bin_SCRIPTS = \
        Audio/calaos_roon

externalprocessdir = $(prefix)/lib/calaos
externalprocess_PYTHON = \
    Audio/ExternProcRoon_main.py

BUILT_SOURCES = Audio/calaos_roon
CLEANFILES = Audio/calaos_roon

install-data-hook:
	chmod +x $(DESTDIR)$(externalprocessdir)/*.py

clean-local:
	rm -f Audio/calaos_roon

EXTRA_DIST = Audio/calaos_roon.in
endif
