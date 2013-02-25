/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
#include <AVRPioneer.h>
#include <AVRManager.h>

using namespace Calaos;

AVRPioneer::AVRPioneer(Params &p):
        AVReceiver(p, 23)
{
        source_names[AVReceiver::AVR_INPUT_BD] = "Blu-ray Disc";
        source_names[AVReceiver::AVR_INPUT_DVD] = "DVD";
        source_names[AVReceiver::AVR_INPUT_TVSAT] = "TV/Sat";
        source_names[AVReceiver::AVR_INPUT_DVRBDR] = "DVR/BDR";
        source_names[AVReceiver::AVR_INPUT_VIDEO_1] = "Video 1";
        source_names[AVReceiver::AVR_INPUT_VIDEO_2] = "Video 2";
        source_names[AVReceiver::AVR_INPUT_HDMI_1] = "HDMI 1";
        source_names[AVReceiver::AVR_INPUT_HDMI_2] = "HDMI 2";
        source_names[AVReceiver::AVR_INPUT_HDMI_3] = "HDMI 3";
        source_names[AVReceiver::AVR_INPUT_HDMI_4] = "HDMI 4";
        source_names[AVReceiver::AVR_INPUT_HDMI_5] = "HDMI 5";
        source_names[AVReceiver::AVR_INPUT_HDMI_6] = "HDMI 6";
        source_names[AVReceiver::AVR_INPUT_NETRADIO] = "Home Media Gallery";
        source_names[AVReceiver::AVR_INPUT_IPOD] = "iPod/USB";
        source_names[AVReceiver::AVR_INPUT_CD] = "CD";
        source_names[AVReceiver::AVR_INPUT_CDRTAPE] = "CD-R/Tape";
        source_names[AVReceiver::AVR_INPUT_TUNER] = "Tuner";
        source_names[AVReceiver::AVR_INPUT_PHONO] = "Phono";
        source_names[AVReceiver::AVR_INPUT_MULTIIN] = "Multi Channel In";
        source_names[AVReceiver::AVR_INPUT_APORT] = "Adapter Port";
        source_names[AVReceiver::AVR_INPUT_SIRIUS] = "Sirius";

        command_suffix = "\r";

        Utils::logger("output") << Priority::INFO << "AVRPioneer::AVRPioneer(" << params["host"] << "): Ok" << log4cpp::eol;
}

AVRPioneer::~AVRPioneer()
{
        Utils::logger("output") << Priority::INFO << "AVRPioneer::~AVRPioneer(): Ok" << log4cpp::eol;
}

void AVRPioneer::connectionEstablished()
{
        sendRequest("?P"); //get power on
        sendRequest("?AP"); //get power on zone 2
        sendRequest("?BP"); //get power on zone 3
        sendRequest("?V"); //get master volume
        sendRequest("?ZV"); //get zone 2 volume
        sendRequest("?YV"); //get zone 3 volume
        sendRequest("?F"); //get input source
        sendRequest("?ZS"); //get input source for zone 2
        sendRequest("?ZT"); //get input source for zone 3
        sendRequest("?FL"); //get display text
}

void AVRPioneer::processMessage(string msg)
{
        Utils::logger("output") << Priority::DEBUG << "AVRPioneer::processMessage(): Recv: " << msg << log4cpp::eol;

        if (msg.substr(0, 3) == "VOL") //volume changed
        {
                msg.erase(0, 3);
                if (is_of_type<int>(msg))
                {
                        from_string(msg, volume_main);
                        volume_main = volume_main * 100 / 185;

                        state_changed_1.emit("volume", to_string(volume_main));
                }
        }
        else if (msg == "PWR1") //power off
        {
                power_main = false;
                state_changed_1.emit("power", "false");
        }
        else if (msg == "PWR0") //power on
        {
                power_main = true;
                state_changed_1.emit("power", "true");
        }
        else if (msg == "APR1") //power zone2 off
        {
                power_zone2 = false;
                state_changed_2.emit("power", "false");
        }
        else if (msg == "APR0") //power zone2 on
        {
                power_zone2 = true;
                state_changed_2.emit("power", "true");
        }
        else if (msg == "BPR1") //power zone3 off
        {
                power_zone3 = false;
                state_changed_3.emit("power", "false");
        }
        else if (msg == "BPR0") //power zone3 on
        {
                power_zone3 = true;
                state_changed_3.emit("power", "true");
        }
        else if (msg.substr(0, 2) == "ZV") //volume zone 2 changed
        {
                msg.erase(0, 2);
                if (is_of_type<int>(msg))
                {
                        from_string(msg, volume_zone2);
                        volume_zone2 = volume_zone2 * 100 / 81;

                        state_changed_2.emit("volume", to_string(volume_zone2));
                }
        }
        else if (msg.substr(0, 2) == "YV") //volume zone 3 changed
        {
                msg.erase(0, 2);
                if (is_of_type<int>(msg))
                {
                        from_string(msg, volume_zone3);
                        volume_zone3 = volume_zone3 * 100 / 81;

                        state_changed_2.emit("volume", to_string(volume_zone3));
                }
        }
        else if (msg.substr(0, 2) == "FL")
        {
                msg.erase(0, 4);
                decodeDisplayText(msg);
        }
        else if (msg.substr(0, 2) == "FN")
        {
                msg.erase(0, 2);
                int in;
                from_string(msg, in);

                switch (in)
                {
                case 25: source_main = AVReceiver::AVR_INPUT_BD; break;
                case 4: source_main = AVReceiver::AVR_INPUT_DVD; break;
                case 5: source_main = AVReceiver::AVR_INPUT_TVSAT; break;
                case 15: source_main = AVReceiver::AVR_INPUT_DVRBDR; break;
                case 10: source_main = AVReceiver::AVR_INPUT_VIDEO_1; break;
                case 14: source_main = AVReceiver::AVR_INPUT_VIDEO_2; break;
                case 19: source_main = AVReceiver::AVR_INPUT_HDMI_1; break;
                case 20: source_main = AVReceiver::AVR_INPUT_HDMI_2; break;
                case 21: source_main = AVReceiver::AVR_INPUT_HDMI_3; break;
                case 22: source_main = AVReceiver::AVR_INPUT_HDMI_4; break;
                case 23: source_main = AVReceiver::AVR_INPUT_HDMI_5; break;
                case 24: source_main = AVReceiver::AVR_INPUT_HDMI_6; break;
                case 26: source_main = AVReceiver::AVR_INPUT_NETRADIO; break;
                case 17: source_main = AVReceiver::AVR_INPUT_IPOD; break;
                case 1: source_main = AVReceiver::AVR_INPUT_CD; break;
                case 3: source_main = AVReceiver::AVR_INPUT_CDRTAPE; break;
                case 2: source_main = AVReceiver::AVR_INPUT_TUNER; break;
                case 0: source_main = AVReceiver::AVR_INPUT_PHONO; break;
                case 12: source_main = AVReceiver::AVR_INPUT_MULTIIN; break;
                case 33: source_main = AVReceiver::AVR_INPUT_APORT; break;
                case 27: source_main = AVReceiver::AVR_INPUT_SIRIUS; break;
                default: return;
                }

                state_changed_1.emit("input_source", to_string(source_main));
        }
        else if (msg.substr(0, 3) == "Z2F")
        {
                msg.erase(0, 3);
                int in;
                from_string(msg, in);

                switch (in)
                {
                case 4: source_zone2 = AVReceiver::AVR_INPUT_DVD; break;
                case 5: source_zone2 = AVReceiver::AVR_INPUT_TVSAT; break;
                case 15: source_zone2 = AVReceiver::AVR_INPUT_DVRBDR; break;
                case 10: source_zone2 = AVReceiver::AVR_INPUT_VIDEO_1; break;
                case 14: source_zone2 = AVReceiver::AVR_INPUT_VIDEO_2; break;
                case 26: source_zone2 = AVReceiver::AVR_INPUT_NETRADIO; break;
                case 17: source_zone2 = AVReceiver::AVR_INPUT_IPOD; break;
                case 1: source_zone2 = AVReceiver::AVR_INPUT_CD; break;
                case 3: source_zone2 = AVReceiver::AVR_INPUT_CDRTAPE; break;
                case 2: source_zone2 = AVReceiver::AVR_INPUT_TUNER; break;
                case 33: source_zone2 = AVReceiver::AVR_INPUT_APORT; break;
                case 27: source_zone2 = AVReceiver::AVR_INPUT_SIRIUS; break;
                default: return;
                }

                state_changed_2.emit("input_source", to_string(source_zone2));
        }
        else if (msg.substr(0, 3) == "Z3F")
        {
                msg.erase(0, 3);
                int in;
                from_string(msg, in);

                switch (in)
                {
                case 4: source_zone3 = AVReceiver::AVR_INPUT_DVD; break;
                case 5: source_zone3 = AVReceiver::AVR_INPUT_TVSAT; break;
                case 15: source_zone3 = AVReceiver::AVR_INPUT_DVRBDR; break;
                case 10: source_zone3 = AVReceiver::AVR_INPUT_VIDEO_1; break;
                case 14: source_zone3 = AVReceiver::AVR_INPUT_VIDEO_2; break;
                case 26: source_zone3 = AVReceiver::AVR_INPUT_NETRADIO; break;
                case 17: source_zone3 = AVReceiver::AVR_INPUT_IPOD; break;
                case 1: source_zone3 = AVReceiver::AVR_INPUT_CD; break;
                case 3: source_zone3 = AVReceiver::AVR_INPUT_CDRTAPE; break;
                case 2: source_zone3 = AVReceiver::AVR_INPUT_TUNER; break;
                case 33: source_zone3 = AVReceiver::AVR_INPUT_APORT; break;
                case 27: source_zone3 = AVReceiver::AVR_INPUT_SIRIUS; break;
                default: return;
                }

                state_changed_3.emit("input_source", to_string(source_zone3));
        }
}

void AVRPioneer::Power(bool on, int zone)
{
        if (zone == 1 && on)
                sendRequest("PO");
        else if (zone == 1 && !on)
                sendRequest("PF");
        else if (zone == 2 && on)
                sendRequest("APO");
        else if (zone == 2 && !on)
                sendRequest("APF");
        else if (zone == 3 && on)
                sendRequest("BPO");
        else if (zone == 3 && !on)
                sendRequest("BPF");
}

void AVRPioneer::setVolume(int volume, int zone)
{
        if (zone == 1)
        {
                int v = volume * 185 / 100;
                stringstream ss;
                ss.width(3);
                ss.fill('0');
                ss << v << "VL";
                sendRequest(ss.str());
        }
        else if (zone == 2)
        {
                int v = volume * 81 / 100;
                stringstream ss;
                ss.width(2);
                ss.fill('0');
                ss << v << "ZV";
                sendRequest(ss.str());
        }
        else if (zone == 3)
        {
                int v = volume * 81 / 100;
                stringstream ss;
                ss.width(2);
                ss.fill('0');
                ss << v << "YV";
                sendRequest(ss.str());
        }
}

void AVRPioneer::decodeDisplayText(string &text)
{
        display_text.clear();
        string tmp;

        for (int i = 0;i < text.length();i++)
        {
                tmp += text[i];
                if (tmp.length() < 2) continue;

                int c;
                istringstream iss(tmp);
                iss >> std::hex >> c;
                display_text += (char)c;
                tmp.clear();
        }

        state_changed_1.emit("display_text", to_string(display_text));
        state_changed_2.emit("display_text", to_string(display_text));
        state_changed_3.emit("display_text", to_string(display_text));
}

void AVRPioneer::selectInputSource(int source, int zone)
{
        if (zone == 1)
        {
                switch (source)
                {
                case AVReceiver::AVR_INPUT_BD: sendRequest("25FN"); break;
                case AVReceiver::AVR_INPUT_DVD: sendRequest("04FN"); break;
                case AVReceiver::AVR_INPUT_TVSAT: sendRequest("05FN"); break;
                case AVReceiver::AVR_INPUT_DVRBDR: sendRequest("15FN"); break;
                case AVReceiver::AVR_INPUT_VIDEO_1: sendRequest("10FN"); break;
                case AVReceiver::AVR_INPUT_VIDEO_2: sendRequest("14FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_1: sendRequest("19FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_2: sendRequest("20FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_3: sendRequest("21FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_4: sendRequest("22FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_5: sendRequest("23FN"); break;
                case AVReceiver::AVR_INPUT_HDMI_6: sendRequest("24FN"); break;
                case AVReceiver::AVR_INPUT_NETRADIO: sendRequest("26FN"); break;
                case AVReceiver::AVR_INPUT_IPOD: sendRequest("17FN"); break;
                case AVReceiver::AVR_INPUT_CD: sendRequest("01FN"); break;
                case AVReceiver::AVR_INPUT_CDRTAPE: sendRequest("03FN"); break;
                case AVReceiver::AVR_INPUT_TUNER: sendRequest("02FN"); break;
                case AVReceiver::AVR_INPUT_PHONO: sendRequest("00FN"); break;
                case AVReceiver::AVR_INPUT_MULTIIN: sendRequest("12FN"); break;
                case AVReceiver::AVR_INPUT_APORT: sendRequest("33FN"); break;
                case AVReceiver::AVR_INPUT_SIRIUS: sendRequest("27FN"); break;
                default: break;
                }
        }
        else if (zone == 2)
        {
                switch (source)
                {
                case AVReceiver::AVR_INPUT_DVD: sendRequest("04ZS"); break;
                case AVReceiver::AVR_INPUT_TVSAT: sendRequest("05ZS"); break;
                case AVReceiver::AVR_INPUT_DVRBDR: sendRequest("15ZS"); break;
                case AVReceiver::AVR_INPUT_VIDEO_1: sendRequest("10ZS"); break;
                case AVReceiver::AVR_INPUT_VIDEO_2: sendRequest("14ZS"); break;
                case AVReceiver::AVR_INPUT_NETRADIO: sendRequest("26ZS"); break;
                case AVReceiver::AVR_INPUT_IPOD: sendRequest("17ZS"); break;
                case AVReceiver::AVR_INPUT_CD: sendRequest("01ZS"); break;
                case AVReceiver::AVR_INPUT_CDRTAPE: sendRequest("03ZS"); break;
                case AVReceiver::AVR_INPUT_TUNER: sendRequest("02ZS"); break;
                case AVReceiver::AVR_INPUT_APORT: sendRequest("33ZS"); break;
                case AVReceiver::AVR_INPUT_SIRIUS: sendRequest("27ZS"); break;
                default: break;
                }
        }
        else if (zone == 3)
        {
                switch (source)
                {
                case AVReceiver::AVR_INPUT_DVD: sendRequest("04ZT"); break;
                case AVReceiver::AVR_INPUT_TVSAT: sendRequest("05ZT"); break;
                case AVReceiver::AVR_INPUT_DVRBDR: sendRequest("15ZT"); break;
                case AVReceiver::AVR_INPUT_VIDEO_1: sendRequest("10ZT"); break;
                case AVReceiver::AVR_INPUT_VIDEO_2: sendRequest("14ZT"); break;
                case AVReceiver::AVR_INPUT_NETRADIO: sendRequest("26ZT"); break;
                case AVReceiver::AVR_INPUT_IPOD: sendRequest("17ZT"); break;
                case AVReceiver::AVR_INPUT_CD: sendRequest("01ZT"); break;
                case AVReceiver::AVR_INPUT_CDRTAPE: sendRequest("03ZT"); break;
                case AVReceiver::AVR_INPUT_TUNER: sendRequest("02ZT"); break;
                case AVReceiver::AVR_INPUT_APORT: sendRequest("33ZT"); break;
                case AVReceiver::AVR_INPUT_SIRIUS: sendRequest("27ZT"); break;
                default: break;
                }
        }
}
