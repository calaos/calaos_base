/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include "AVROnkyo.h"

using namespace Calaos;

AVROnkyo::AVROnkyo(Params &p):
    AVReceiver(p, 60128, AVR_CON_BYTES)
{
    source_names[AVReceiver::AVR_INPUT_DVD] = "DVD / Bluray";
    source_names[AVReceiver::AVR_INPUT_CD] = "CD";
    source_names[AVReceiver::AVR_INPUT_PHONO] = "Phono";
    source_names[AVReceiver::AVR_INPUT_TUNER] = "Tuner";
    source_names[AVReceiver::AVR_INPUT_VIDEO_1] = "VCR/DVR";
    source_names[AVReceiver::AVR_INPUT_VIDEO_2] = "Sat/CBL";
    source_names[AVReceiver::AVR_INPUT_VIDEO_3] = "Game/TV";
    source_names[AVReceiver::AVR_INPUT_VIDEO_4] = "Aux. 1";
    source_names[AVReceiver::AVR_INPUT_VIDEO_5] = "Aux. 2";
    source_names[AVReceiver::AVR_INPUT_VIDEO_6] = "PC";
    source_names[AVReceiver::AVR_INPUT_VIDEO_7] = "Video 7";
    source_names[AVReceiver::AVR_INPUT_TV] = "TV/Tape";
    source_names[AVReceiver::AVR_INPUT_SERVER] = "Music Server DLNA";
    source_names[AVReceiver::AVR_INPUT_NETRADIO] = "Internet Radio";
    source_names[AVReceiver::AVR_INPUT_USB] = "USB Front";
    source_names[AVReceiver::AVR_INPUT_USB2] = "USB Rear";
    source_names[AVReceiver::AVR_INPUT_NETWORK] = "Network";
    source_names[AVReceiver::AVR_INPUT_APORT] = "Universal Port";
    source_names[AVReceiver::AVR_INPUT_MULTIIN] = "Multi Ch. In";

    cInfoDom("output") << "AVROnkyo::AVROnkyo(" << params["host"] << "): Ok";
}

AVROnkyo::~AVROnkyo()
{
}

void AVROnkyo::connectionEstablished()
{
    //power status?
    sendCustomCommand("PWRQSTN");
    sendCustomCommand("ZPWQSTN");
    sendCustomCommand("PW3QSTN");

    //input selected?
    sendCustomCommand("SLIQSTN");
    sendCustomCommand("SLZQSTN");
    sendCustomCommand("SL3QSTN");

    //volume?
    sendCustomCommand("MVLQSTN");
    sendCustomCommand("ZVLQSTN");
    sendCustomCommand("VL3QSTN");
}

void AVROnkyo::sendCustomCommand(string command)
{
    vector<char> eISCP;

    int data_size = command.length() + 3; //2 more bytes for !1
    //        int msg_size = data_size + 16; //end char + header

    //Header
    eISCP.push_back('I');
    eISCP.push_back('S');
    eISCP.push_back('C');
    eISCP.push_back('P');

    //Header size (big endian)
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);
    eISCP.push_back(0x10);

    //data size (big endian)
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);
    // the official ISCP docs say this is supposed to be just the data size  (eiscpDataSize)
    // ** BUT **
    // It only works if you send the size of the entire Message size (eiscpMsgSize)
    eISCP.push_back((char)data_size);

    //eISCP version
    eISCP.push_back(0x01);

    //reserved
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);
    eISCP.push_back(0x00);

    //data header
    eISCP.push_back('!');
    eISCP.push_back('1'); //device type, 1 for receiver

    //data command
    eISCP.insert(eISCP.end(), command.begin(), command.end());

    //end char
    eISCP.push_back(0x0D);

    sendRequest(eISCP);
}

void AVROnkyo::processMessage(vector<char> data)
{
    if (data.size() < 18) //msg is too small, wait for more data to come
    {
        //We don't have a complete paquet yet, buffurize it.
        brecv_buffer.insert(brecv_buffer.end(), data.begin(), data.end());

        cDebugDom("output") << "Bufferize data.";

        return;
    }

    if (!brecv_buffer.empty())
    {
        //Put last data in buffer
        brecv_buffer.insert(data.end(), data.begin(), data.end());
        data = brecv_buffer;
        brecv_buffer.clear();
    }

    vector<char>::iterator it = data.begin();
    string msg;
    for (int i = 0;it < data.end();it++, i++)
    {
        if (i < 18) continue;

        if (*it == 0x0A ||
            *it == 0x0D ||
            *it == 0x1A)
        {
            if (!msg.empty())
            {
                processMessage(msg);
                data.erase(data.begin(), it);

                //remove starting separator chars
                while (data.size() > 0 &&
                       (*(data.begin()) == 0x0A ||
                        *(data.begin()) == 0x1A ||
                        *(data.begin()) == 0x0D))
                {
                    data.erase(data.begin());
                }

                it = data.begin();
                i = 0;
                msg.clear();
            }
            continue;
        }
        msg.push_back(*it);
    }

    if (!data.empty())
    {
        //We don't have a complete paquet yet, buffurize it.
        brecv_buffer.insert(data.end(), data.begin(), data.end());

        cDebugDom("output") << "Bufferize data.";
    }
}

void AVROnkyo::processMessage(string msg)
{
    cDebugDom("output") << "Recv new message: " << msg;

    if (msg.substr(0, 3) == "MVL") //master volume changed
    {
        msg.erase(0, 3);
        if (is_of_type<int>(msg))
        {
            istringstream iss(msg);
            iss >> hex >> volume_main;

            state_changed_1.emit("volume", Utils::to_string(volume_main));
        }
    }
    else if (msg.substr(0, 3) == "ZVL") //zone2 volume changed
    {
        msg.erase(0, 3);
        if (is_of_type<int>(msg))
        {
            istringstream iss(msg);
            iss >> hex >> volume_zone2;

            state_changed_2.emit("volume", Utils::to_string(volume_zone2));
        }
    }
    else if (msg.substr(0, 3) == "VL3") //zone3 volume changed
    {
        msg.erase(0, 3);
        if (is_of_type<int>(msg))
        {
            istringstream iss(msg);
            iss >> hex >> volume_zone3;

            state_changed_3.emit("volume", Utils::to_string(volume_zone3));
        }
    }
    else if (msg == "PWR00") //power off
    {
        power_main = false;
        state_changed_1.emit("power", "false");
    }
    else if (msg == "PWR01") //power on
    {
        power_main = true;
        state_changed_1.emit("power", "true");
    }
    else if (msg == "ZPW00") //power off
    {
        power_zone2 = false;
        state_changed_2.emit("power", "false");
    }
    else if (msg == "ZPW01") //power on
    {
        power_zone2 = true;
        state_changed_2.emit("power", "true");
    }
    else if (msg == "PW300") //power off
    {
        power_zone3 = false;
        state_changed_3.emit("power", "false");
    }
    else if (msg == "PW301") //power on
    {
        power_zone3 = true;
        state_changed_3.emit("power", "true");
    }
    else if (msg.substr(0, 3) == "SLI") //main zone input source changed
    {
        msg.erase(0, 3);
        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_main = inputFromString(msg);
            state_changed_1.emit("input_source", Utils::to_string(source_main));
        }
    }
    else if (msg.substr(0, 3) == "SLZ") //zone 2 input source changed
    {
        msg.erase(0, 3);
        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_zone2 = inputFromString(msg);
            state_changed_2.emit("input_source", Utils::to_string(source_main));
        }
    }
    else if (msg.substr(0, 3) == "SL3") //zone 3 input source changed
    {
        msg.erase(0, 3);
        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_zone3 = inputFromString(msg);
            state_changed_3.emit("input_source", Utils::to_string(source_main));
        }
    }

}

void AVROnkyo::Power(bool on, int zone)
{
    if (zone == 1 && on)
        sendCustomCommand("PWR01");
    else if (zone == 1 && !on)
        sendCustomCommand("PWR00");
    else if (zone == 2 && on)
        sendCustomCommand("ZPW01");
    else if (zone == 2 && !on)
        sendCustomCommand("ZPW00");
    else if (zone == 3 && on)
        sendCustomCommand("PW301");
    else if (zone == 3 && !on)
        sendCustomCommand("PW300");
}

int AVROnkyo::inputFromString(string source)
{
    if (source == "00") return AVReceiver::AVR_INPUT_VIDEO_1;
    if (source == "01") return AVReceiver::AVR_INPUT_VIDEO_2;
    if (source == "02") return AVReceiver::AVR_INPUT_VIDEO_3;
    if (source == "03") return AVReceiver::AVR_INPUT_VIDEO_4;
    if (source == "04") return AVReceiver::AVR_INPUT_VIDEO_5;
    if (source == "05") return AVReceiver::AVR_INPUT_VIDEO_6;
    if (source == "06") return AVReceiver::AVR_INPUT_VIDEO_7;
    if (source == "10") return AVReceiver::AVR_INPUT_DVD;
    if (source == "20") return AVReceiver::AVR_INPUT_TV;
    if (source == "22") return AVReceiver::AVR_INPUT_PHONO;
    if (source == "23") return AVReceiver::AVR_INPUT_CD;
    if (source == "26") return AVReceiver::AVR_INPUT_TUNER;
    if (source == "27") return AVReceiver::AVR_INPUT_SERVER;
    if (source == "28") return AVReceiver::AVR_INPUT_NETRADIO;
    if (source == "29") return AVReceiver::AVR_INPUT_USB;
    if (source == "2A") return AVReceiver::AVR_INPUT_USB2;
    if (source == "2B") return AVReceiver::AVR_INPUT_NETWORK;
    if (source == "40") return AVReceiver::AVR_INPUT_APORT;
    if (source == "30") return AVReceiver::AVR_INPUT_MULTIIN;

    return AVReceiver::AVR_UNKNOWN;
}

string AVROnkyo::inputToString(int source)
{
    switch (source)
    {
    case AVReceiver::AVR_INPUT_VIDEO_1: return "00";
    case AVReceiver::AVR_INPUT_VIDEO_2: return "01";
    case AVReceiver::AVR_INPUT_VIDEO_3: return "02";
    case AVReceiver::AVR_INPUT_VIDEO_4: return "03";
    case AVReceiver::AVR_INPUT_VIDEO_5: return "04";
    case AVReceiver::AVR_INPUT_VIDEO_6: return "05";
    case AVReceiver::AVR_INPUT_VIDEO_7: return "06";
    case AVReceiver::AVR_INPUT_DVD: return "10";
    case AVReceiver::AVR_INPUT_TV: return "20";
    case AVReceiver::AVR_INPUT_PHONO: return "22";
    case AVReceiver::AVR_INPUT_CD: return "23";
    case AVReceiver::AVR_INPUT_TUNER: return "26";
    case AVReceiver::AVR_INPUT_SERVER: return "27";
    case AVReceiver::AVR_INPUT_NETRADIO: return "28";
    case AVReceiver::AVR_INPUT_USB: return "29";
    case AVReceiver::AVR_INPUT_USB2: return "2A";
    case AVReceiver::AVR_INPUT_NETWORK: return "2B";
    case AVReceiver::AVR_INPUT_APORT: return "40";
    case AVReceiver::AVR_INPUT_MULTIIN: return "30";
    default: break;
    }

    return "";
}

void AVROnkyo::setVolume(int volume, int zone)
{
    stringstream ss;
    ss.width(2);
    ss.fill('0');

    if (zone == 1) ss << "MVL" << uppercase << hex << volume;
    else if (zone == 2) ss << "ZVL" << uppercase << hex << volume;
    else if (zone == 3) ss << "VL3" << uppercase << hex << volume;
    else return;

    sendCustomCommand(ss.str());
}

void AVROnkyo::selectInputSource(int source, int zone)
{
    string s = inputToString(source);
    if (s == "") return;

    string cmd;
    if (zone == 1) cmd = "SLI" + s;
    else if (zone == 2) cmd = "SLZ" + s;
    else if (zone == 3) cmd = "SL3" + s;
    else return;

    sendCustomCommand(cmd);
}
