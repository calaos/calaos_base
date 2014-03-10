/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "AVRDenon.h"

using namespace Calaos;

AVRDenon::AVRDenon(Params &p):
    AVReceiver(p, 23)
{
    source_names[AVReceiver::AVR_INPUT_DVD] = "DVD";
    source_names[AVReceiver::AVR_INPUT_BD] = "Blu-ray Disc";
    source_names[AVReceiver::AVR_INPUT_CD] = "CD";
    source_names[AVReceiver::AVR_INPUT_PHONO] = "Phono";
    source_names[AVReceiver::AVR_INPUT_TUNER] = "Tuner";
    source_names[AVReceiver::AVR_INPUT_TV] = "TV";
    source_names[AVReceiver::AVR_INPUT_SAT] = "Sat/CBL";
    source_names[AVReceiver::AVR_INPUT_DVRBDR] = "DVR";
    source_names[AVReceiver::AVR_INPUT_GAME_1] = "Game";
    source_names[AVReceiver::AVR_INPUT_GAME_2] = "Game 2";
    source_names[AVReceiver::AVR_INPUT_AUX] = "V. Aux";
    source_names[AVReceiver::AVR_INPUT_DOCK] = "Dock";
    source_names[AVReceiver::AVR_INPUT_HDRADIO] = "HD Radio";
    source_names[AVReceiver::AVR_INPUT_IPOD] = "iPod";
    source_names[AVReceiver::AVR_INPUT_USB] = "Net/USB";
    source_names[AVReceiver::AVR_INPUT_RHAPSODY] = "Rhapsody";
    source_names[AVReceiver::AVR_INPUT_NAPSTER] = "Napster";
    source_names[AVReceiver::AVR_INPUT_PANDORA] = "Pandora";
    source_names[AVReceiver::AVR_INPUT_LASTFM] = "LastFM";
    source_names[AVReceiver::AVR_INPUT_FLICKR] = "Flickr";
    source_names[AVReceiver::AVR_INPUT_FAV] = "Favorites";
    source_names[AVReceiver::AVR_INPUT_NETRADIO] = "IRadio";
    source_names[AVReceiver::AVR_INPUT_SERVER] = "Server";

    command_suffix = "\r";

    cInfoDom("output") << "AVRDenon::AVRDenon(" << params["host"] << "): Ok";
}

AVRDenon::~AVRDenon()
{
    cInfoDom("output") << "AVRDenon::~AVRDenon(): Ok";
}

void AVRDenon::connectionEstablished()
{
    //power status?
    sendRequest("PW?");
    sendRequest("Z2?");
    sendRequest("Z3?");

    //input selected?
    sendRequest("SI?");

    //volume?
    sendRequest("MV?");
}

void AVRDenon::processMessage(string msg)
{
    cDebugDom("output") << "AVRDenon::processMessage(): Recv: " << msg;

    if (msg.substr(0, 2) == "MV") //master volume changed
    {
        msg.erase(0, 2);
        if (is_of_type<int>(msg))
        {
            from_string(msg, volume_main);
            volume_main = 99 - volume_main;
            if (msg.length() == 2)
                volume_main = volume_main * 100 / 99;
            else
                volume_main = (volume_main / 10) * 100 / 99;

            state_changed_1.emit("volume", Utils::to_string(volume_main));
        }
    }
    else if (msg == "PWSTANDBY") //power off
    {
        power_main = false;
        state_changed_1.emit("power", "false");
    }
    else if (msg == "PWON") //power on
    {
        power_main = true;
        state_changed_1.emit("power", "true");
    }
    else if (msg.substr(0, 2) == "Z2") //zone 2 changed
    {
        msg.erase(0, 2);

        if (msg == "ON")
        {
            power_zone2 = true;
            state_changed_2.emit("power", "true");
        }
        else if (msg == "OFF")
        {
            power_zone2 = false;
            state_changed_2.emit("power", "false");
        }
        else if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_zone2 = inputFromString(msg);
            state_changed_2.emit("input_source", Utils::to_string(source_zone2));
        }
        else if (is_of_type<int>(msg)) //Volume changed
        {
            from_string(msg, volume_zone2);
            volume_zone2 = 99 - volume_zone2;
            if (msg.length() == 2)
                volume_zone2 = volume_zone2 * 100 / 99;
            else
                volume_zone2 = (volume_zone2 / 10) * 100 / 99;

            state_changed_2.emit("volume", Utils::to_string(volume_zone2));
        }
    }
    else if (msg.substr(0, 2) == "Z3") //zone 3 changed
    {
        msg.erase(0, 2);

        if (msg == "ON")
        {
            power_zone3 = true;
            state_changed_3.emit("power", "true");
        }
        else if (msg == "OFF")
        {
            power_zone3 = false;
            state_changed_3.emit("power", "false");
        }
        else if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_zone3 = inputFromString(msg);
            state_changed_3.emit("input_source", Utils::to_string(source_zone3));
        }
        else if (is_of_type<int>(msg)) //Volume changed
        {
            from_string(msg, volume_zone3);
            volume_zone3 = 99 - volume_zone3;
            if (msg.length() == 2)
                volume_zone3 = volume_zone3 * 100 / 99;
            else
                volume_zone3 = (volume_zone3 / 10) * 100 / 99;

            state_changed_3.emit("volume", Utils::to_string(volume_zone3));
        }
    }
    else if (msg.substr(0, 2) == "SI") //main zone input source changed
    {
        msg.erase(0, 2);
        if (inputFromString(msg) != AVReceiver::AVR_UNKNOWN) //this is an input source change
        {
            source_main = inputFromString(msg);
            state_changed_1.emit("input_source", Utils::to_string(source_main));
        }
    }
}

void AVRDenon::Power(bool on, int zone)
{
    if (zone == 1 && on)
        sendRequest("PWON");
    else if (zone == 1 && !on)
        sendRequest("PWSTANDBY");
    else if (zone == 2 && on)
        sendRequest("Z2ON");
    else if (zone == 2 && !on)
        sendRequest("Z2OFF");
    else if (zone == 3 && on)
        sendRequest("Z3ON");
    else if (zone == 3 && !on)
        sendRequest("Z3OFF");
}

int AVRDenon::inputFromString(string source)
{
    if (source == "PHONO") return AVReceiver::AVR_INPUT_PHONO;
    if (source == "CD") return AVReceiver::AVR_INPUT_CD;
    if (source == "TUNER") return AVReceiver::AVR_INPUT_TUNER;
    if (source == "DVD") return AVReceiver::AVR_INPUT_DVD;
    if (source == "BD") return AVReceiver::AVR_INPUT_BD;
    if (source == "TV") return AVReceiver::AVR_INPUT_TV;
    if (source == "SAT/CBL") return AVReceiver::AVR_INPUT_SAT;
    if (source == "DVR") return AVReceiver::AVR_INPUT_DVRBDR;
    if (source == "GAME") return AVReceiver::AVR_INPUT_GAME_1;
    if (source == "GAME2") return AVReceiver::AVR_INPUT_GAME_2;
    if (source == "V.AUX") return AVReceiver::AVR_INPUT_AUX;
    if (source == "DOCK") return AVReceiver::AVR_INPUT_DOCK;
    if (source == "HDRADIO") return AVReceiver::AVR_INPUT_HDRADIO;
    if (source == "IPOD") return AVReceiver::AVR_INPUT_IPOD;
    if (source == "NET/USB") return AVReceiver::AVR_INPUT_USB;
    if (source == "RHAPSODY") return AVReceiver::AVR_INPUT_RHAPSODY;
    if (source == "NAPSTER") return AVReceiver::AVR_INPUT_NAPSTER;
    if (source == "PANDORA") return AVReceiver::AVR_INPUT_PANDORA;
    if (source == "LASTFM") return AVReceiver::AVR_INPUT_LASTFM;
    if (source == "FLICKR") return AVReceiver::AVR_INPUT_FLICKR;
    if (source == "FAVORITES") return AVReceiver::AVR_INPUT_FAV;
    if (source == "IRADIO") return AVReceiver::AVR_INPUT_NETRADIO;
    if (source == "SERVER") return AVReceiver::AVR_INPUT_SERVER;

    return AVReceiver::AVR_UNKNOWN;
}

string AVRDenon::inputToString(int source)
{
    switch (source)
    {
    case AVReceiver::AVR_INPUT_PHONO: return "PHONO";
    case AVReceiver::AVR_INPUT_CD: return "CD";
    case AVReceiver::AVR_INPUT_TUNER: return "TUNER";
    case AVReceiver::AVR_INPUT_DVD: return "DVD";
    case AVReceiver::AVR_INPUT_BD: return "BD";
    case AVReceiver::AVR_INPUT_TV: return "TV";
    case AVReceiver::AVR_INPUT_SAT: return "SAT/CBL";
    case AVReceiver::AVR_INPUT_DVRBDR: return "DVR";
    case AVReceiver::AVR_INPUT_GAME_1: return "GAME";
    case AVReceiver::AVR_INPUT_GAME_2: return "GAME2";
    case AVReceiver::AVR_INPUT_AUX: return "V.AUX";
    case AVReceiver::AVR_INPUT_DOCK: return "DOCK";
    case AVReceiver::AVR_INPUT_HDRADIO: return "HDRADIO";
    case AVReceiver::AVR_INPUT_IPOD: return "IPOD";
    case AVReceiver::AVR_INPUT_USB: return "NET/USB";
    case AVReceiver::AVR_INPUT_RHAPSODY: return "RHAPSODY";
    case AVReceiver::AVR_INPUT_NAPSTER: return "NAPSTER";
    case AVReceiver::AVR_INPUT_PANDORA: return "PANDORA";
    case AVReceiver::AVR_INPUT_LASTFM: return "LASTFM";
    case AVReceiver::AVR_INPUT_FLICKR: return "FLICKR";
    case AVReceiver::AVR_INPUT_FAV: return "FAVORITES";
    case AVReceiver::AVR_INPUT_NETRADIO: return "IRADIO";
    case AVReceiver::AVR_INPUT_SERVER: return "SERVER";
    default: break;
    }

    return "";
}

void AVRDenon::setVolume(int volume, int zone)
{
    int v = volume * 99 / 100;
    v = 99 - v;
    stringstream ss;
    ss.width(2);
    ss.fill('0');

    if (zone == 1) ss << "MV" << v;
    else if (zone == 2) ss << "Z2" << v;
    else if (zone == 3) ss << "Z3" << v;
    else return;

    sendRequest(ss.str());
}

void AVRDenon::selectInputSource(int source, int zone)
{
    string s = inputToString(source);
    if (s == "") return;

    string cmd;
    if (zone == 1) cmd = "SI" + s;
    else if (zone == 2) cmd = "Z2" + s;
    else if (zone == 3) cmd = "Z3" + s;
    else return;

    sendRequest(cmd);
}
