/******************************************************************************
**  Copyright (c) 2006-2013, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "AVRYamaha.h"
#include <cmath>

using namespace Calaos;

AVRYamaha::AVRYamaha(Params &p):
        AVReceiver(p, 50000)
{

        source_names[AVReceiver::AVR_INPUT_HDMI_1] = "HDMI 1";
        source_names[AVReceiver::AVR_INPUT_HDMI_2] = "HDMI 2";
        source_names[AVReceiver::AVR_INPUT_HDMI_3] = "HDMI 3";
        source_names[AVReceiver::AVR_INPUT_HDMI_4] = "HDMI 4";
        source_names[AVReceiver::AVR_INPUT_HDMI_5] = "HDMI 5";
        source_names[AVReceiver::AVR_INPUT_TUNER] = "Tuner";
        source_names[AVReceiver::AVR_INPUT_PHONO] = "Phono";
        source_names[AVReceiver::AVR_INPUT_VIDEO_1] = "Video 1";
        source_names[AVReceiver::AVR_INPUT_VIDEO_2] = "Video 2";
        source_names[AVReceiver::AVR_INPUT_VIDEO_3] = "Video 3";
        source_names[AVReceiver::AVR_INPUT_VIDEO_4] = "Video 4";
        source_names[AVReceiver::AVR_INPUT_VIDEO_5] = "Video 5";
        source_names[AVReceiver::AVR_INPUT_VIDEO_6] = "Video 6";
        source_names[AVReceiver::AVR_INPUT_AUX] = "V-Aux";
        source_names[AVReceiver::AVR_INPUT_AUX1] = "Audio 1";
        source_names[AVReceiver::AVR_INPUT_AUX2] = "Audio 2";
        source_names[AVReceiver::AVR_INPUT_DOCK] = "Dock";
        source_names[AVReceiver::AVR_INPUT_IPOD] = "iPod";
        source_names[AVReceiver::AVR_INPUT_BLUETOOTH] = "Bluetooth";
        source_names[AVReceiver::AVR_INPUT_NETWORK] = "Network";
        source_names[AVReceiver::AVR_INPUT_NAPSTER] = "Napster";
        source_names[AVReceiver::AVR_INPUT_NETRADIO] = "Net Radio";
        source_names[AVReceiver::AVR_INPUT_USB] = "USB";
        source_names[AVReceiver::AVR_INPUT_IPODUSB] = "iPod (USB)";
        source_names[AVReceiver::AVR_INPUT_PC] = "PC";
        source_names[AVReceiver::AVR_INPUT_UAW] = "UAW";

        command_suffix = "\r\n";

        cInfoDom("output") << "AVRYamaha::AVRYamaha(" << params["host"] << "): Ok" << log4cpp::eol;
}

AVRYamaha::~AVRYamaha()
{
        cInfoDom("output") << "AVRYamaha::~AVRYamaha(): Ok" << log4cpp::eol;
}

void AVRYamaha::connectionEstablished()
{
        //power status?
        sendRequest("@MAIN:PWR=?");
        sendRequest("@ZONE2:PWR=?");
        sendRequest("@ZONE3:PWR=?");

        //input selected?
        sendRequest("@MAIN:INP=?");
        sendRequest("@ZONE2:INP=?");
        sendRequest("@ZONE3:INP=?");

        //volume?
        sendRequest("@MAIN:VOL=?");
        sendRequest("@ZONE2:VOL=?");
        sendRequest("@ZONE3:VOL=?");
}

void AVRYamaha::processMessage(string msg)
{
        cDebugDom("output") << "AVRYamaha::processMessage(): Recv: " << msg << log4cpp::eol;

        if (msg.substr(0, 6) == "@MAIN:")
        {
                msg.erase(0, 6);

                if (msg.substr(0, 4) == "VOL=")
                {
                        msg.erase(0, 4);
                        if (is_of_type<double>(msg))
                        {
                                double v;
                                    from_string(msg, v);
                                    v += 80.5;
                                    v = v * 100 / 97;
                                    volume_main = (int)v;

                                state_changed_1.emit("volume", Utils::to_string(volume_main));
                        }
                }
                else if (msg == "PWR=On")
                {
                        power_main = true;
                        state_changed_1.emit("power", "true");
                }
                else if (msg == "PWR=Standby")
                {
                        power_main = false;
                        state_changed_1.emit("power", "false");
                }
                else if (msg.substr(0, 4) == "INP=")
                {
                        msg.erase(0, 4);
                        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN)
                        {
                                source_main = inputFromString(msg);
                                state_changed_1.emit("input_source", Utils::to_string(source_main));
                        }
                }
        }
        else if (msg.substr(0, 7) == "@ZONE2:")
        {
                msg.erase(0, 7);

                if (msg.substr(0, 4) == "VOL=")
                {
                        msg.erase(0, 4);
                        if (is_of_type<double>(msg))
                        {
                                double v;
                                    from_string(msg, v);
                                    v += 80.5;
                                    v = v * 100 / 97;
                                    volume_zone2 = (int)v;

                                state_changed_2.emit("volume", Utils::to_string(volume_zone2));
                        }
                }
                else if (msg == "PWR=On")
                {
                        power_zone2 = true;
                        state_changed_2.emit("power", "true");
                }
                else if (msg == "PWR=Standby")
                {
                        power_zone2 = false;
                        state_changed_2.emit("power", "false");
                }
                 else if (msg.substr(0, 4) == "INP=")
                {
                        msg.erase(0, 4);
                        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN)
                        {
                                source_zone2 = inputFromString(msg);
                                state_changed_2.emit("input_source", Utils::to_string(source_zone2));
                        }
                }
        }
        else if (msg.substr(0, 7) == "@ZONE3:")
        {
                msg.erase(0, 7);

                if (msg.substr(0, 4) == "VOL=")
                {
                        msg.erase(0, 4);
                        if (is_of_type<double>(msg))
                        {
                                double v;
                                    from_string(msg, v);
                                    v += 80.5;
                                    v = v * 100 / 97;
                                    volume_zone3 = (int)v;

                                state_changed_3.emit("volume", Utils::to_string(volume_zone3));
                        }
                }
                else if (msg == "PWR=On")
                {
                        power_zone3 = true;
                        state_changed_3.emit("power", "true");
                }
                else if (msg == "PWR=Standby")
                {
                        power_zone3 = false;
                        state_changed_3.emit("power", "false");
                }
                else if (msg.substr(0, 4) == "INP=")
                {
                        msg.erase(0, 4);
                        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN)
                        {
                                source_zone3 = inputFromString(msg);
                                state_changed_3.emit("input_source", Utils::to_string(source_zone3));
                        }
                }
        }
}

void AVRYamaha::Power(bool on, int zone)
{
        if (zone == 1 && on)
                sendRequest("@MAIN:PWR=On");
        else if (zone == 1 && !on)
                sendRequest("@MAIN:PWR=Standby");
        else if (zone == 2 && on)
                sendRequest("@ZONE2:PWR=On");
        else if (zone == 2 && !on)
                sendRequest("@ZONE2:PWR=Standby");
        else if (zone == 3 && on)
                sendRequest("@ZONE3:PWR=On");
        else if (zone == 3 && !on)
                sendRequest("@ZONE3:PWR=Standby");
}

int AVRYamaha::inputFromString(string source)
{
        if (source == "TUNER") return AVReceiver::AVR_INPUT_TUNER;
        if (source == "PHONO") return AVReceiver::AVR_INPUT_PHONO;
        if (source == "HDMI1") return AVReceiver::AVR_INPUT_HDMI_1;
        if (source == "HDMI2") return AVReceiver::AVR_INPUT_HDMI_2;
        if (source == "HDMI3") return AVReceiver::AVR_INPUT_HDMI_3;
        if (source == "HDMI4") return AVReceiver::AVR_INPUT_HDMI_4;
        if (source == "HDMI5") return AVReceiver::AVR_INPUT_HDMI_5;
        if (source == "AV1") return AVReceiver::AVR_INPUT_VIDEO_1;
        if (source == "AV2") return AVReceiver::AVR_INPUT_VIDEO_2;
        if (source == "AV3") return AVReceiver::AVR_INPUT_VIDEO_3;
        if (source == "AV4") return AVReceiver::AVR_INPUT_VIDEO_4;
        if (source == "AV5") return AVReceiver::AVR_INPUT_VIDEO_5;
        if (source == "AV6") return AVReceiver::AVR_INPUT_VIDEO_6;
        if (source == "V-AUX") return AVReceiver::AVR_INPUT_AUX;
        if (source == "AUDIO1") return AVReceiver::AVR_INPUT_AUX1;
        if (source == "AUDIO2") return AVReceiver::AVR_INPUT_AUX2;
        if (source == "DOCK") return AVReceiver::AVR_INPUT_DOCK;
        if (source == "iPod") return AVReceiver::AVR_INPUT_IPOD;
        if (source == "Bluetooth") return AVReceiver::AVR_INPUT_BLUETOOTH;
        if (source == "UAW") return AVReceiver::AVR_INPUT_UAW;
        if (source == "NET") return AVReceiver::AVR_INPUT_NETWORK;
        if (source == "Napster") return AVReceiver::AVR_INPUT_NAPSTER;
        if (source == "PC") return AVReceiver::AVR_INPUT_PC;
        if (source == "NET RADIO") return AVReceiver::AVR_INPUT_NETRADIO; ///TODO: check if the answer is "NET RADIO" or "NETRADIO". doc said space is not allowed, but there is a space here...?
        if (source == "USB") return AVReceiver::AVR_INPUT_USB;
        if (source == "iPod (USB)") return AVReceiver::AVR_INPUT_IPODUSB; ///TODO: same space question here?
        
        return AVReceiver::AVR_UNKNOWN;
}

string AVRYamaha::inputToString(int source)
{
        switch (source)
        {
        case AVReceiver::AVR_INPUT_TUNER: return "TUNER";
        case AVReceiver::AVR_INPUT_PHONO: return "PHONO";
        case AVReceiver::AVR_INPUT_HDMI_1: return "HDMI1";
        case AVReceiver::AVR_INPUT_HDMI_2: return "HDMI2";
        case AVReceiver::AVR_INPUT_HDMI_3: return "HDMI3";
        case AVReceiver::AVR_INPUT_HDMI_4: return "HDMI4";
        case AVReceiver::AVR_INPUT_HDMI_5: return "HDMI5";
        case AVReceiver::AVR_INPUT_VIDEO_1: return "AV1";
        case AVReceiver::AVR_INPUT_VIDEO_2: return "AV2";
        case AVReceiver::AVR_INPUT_VIDEO_3: return "AV3";
        case AVReceiver::AVR_INPUT_VIDEO_4: return "AV4";
        case AVReceiver::AVR_INPUT_VIDEO_5: return "AV5";
        case AVReceiver::AVR_INPUT_VIDEO_6: return "AV6";
        case AVReceiver::AVR_INPUT_AUX: return "V-AUX";
        case AVReceiver::AVR_INPUT_AUX1: return "AUDIO1";
        case AVReceiver::AVR_INPUT_AUX2: return "AUDIO2";
        case AVReceiver::AVR_INPUT_DOCK: return "DOCK";
        case AVReceiver::AVR_INPUT_IPOD: return "iPod";
        case AVReceiver::AVR_INPUT_BLUETOOTH: return "Bluetooth";
        case AVReceiver::AVR_INPUT_UAW: return "UAW";
        case AVReceiver::AVR_INPUT_NETWORK: return "NET";
        case AVReceiver::AVR_INPUT_NAPSTER: return "Napster";
        case AVReceiver::AVR_INPUT_PC: return "PC";
        case AVReceiver::AVR_INPUT_NETRADIO: return "NET RADIO"; ///TODO, space?
        case AVReceiver::AVR_INPUT_USB: return "USB";
        case AVReceiver::AVR_INPUT_IPODUSB: return "iPod (USB)"; ///TODO, space?
        default: break;
        }

        return "";
}

void AVRYamaha::setVolume(int volume, int zone)
{
        //The range is -80.5 -> 16.5
        //and the step should be always 0.5
        double v = volume * 97 / 100;
        v -= 80.5;
        v = std::floor(v / 0.5) * 0.5;

        stringstream ss;
        ss.width(2);
        ss.fill('0');
        ss.setf(std::ios::showpoint);
        ss.precision(1);

        if (zone == 1) ss << "@MAIN:VOL=" << std::fixed << v;
        else if (zone == 2) ss << "@ZONE2:VOL=" << v;
        else if (zone == 3) ss << "@ZONE3:VOL=" << v;
        else return;

        sendRequest(ss.str());
}

void AVRYamaha::selectInputSource(int source, int zone)
{
        string s = inputToString(source);
        if (s == "") return;

        string cmd;
        if (zone == 1) cmd = "@MAIN:INP=" + s;
        else if (zone == 2) cmd = "@ZONE2:INP=" + s;
        else if (zone == 3) cmd = "@ZONE3:INP=" + s;
        else return;

        sendRequest(cmd);
}

