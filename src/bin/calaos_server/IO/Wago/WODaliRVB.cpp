/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <WODaliRVB.h>
#include <IPC.h>

using namespace Calaos;

WODaliRVB::WODaliRVB(Params &_p):
                OutputLightRGB(_p),
                port(502)
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        //reqd initial state
        string cmd;
        cmd = "WAGO_DALI_GET " + get_param("rline") + " " + get_param("raddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandRed_cb));
        cmd = "WAGO_DALI_GET " + get_param("gline") + " " + get_param("gaddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandGreen_cb));
        cmd = "WAGO_DALI_GET " + get_param("bline") + " " + get_param("baddress");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandBlue_cb));

        Calaos::StartReadRules::Instance().addIO();
        Calaos::StartReadRules::Instance().addIO();
        Calaos::StartReadRules::Instance().addIO();

        cDebugDom("output") << "WODaliRVB::WODaliRVB(" << get_param("id") << "): Ok";
}

WODaliRVB::~WODaliRVB()
{
        cDebugDom("output") << "WODaliRVB::~WODaliRVB(): Ok";
}

void WODaliRVB::WagoUDPCommandRed_cb(bool status, string command, string result)
{
        if (!status)
        {
                cInfoDom("output") << "WODaliRVB::WagoUdpCommandRed(): Error with request " << command;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], red);
                red = (red * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                emitChange();
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandGreen_cb(bool status, string command, string result)
{
        if (!status)
        {
                cInfoDom("output") << "WODaliRVB::WagoUdpCommandGreen(): Error with request " << command;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], green);
                green = (green * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                emitChange();
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandBlue_cb(bool status, string command, string result)
{
        if (!status)
        {
                cInfoDom("output") << "WODaliRVB::WagoUdpCommandBlue(): Error with request " << command;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        vector<string> tokens;
        split(result, tokens);
        if (tokens.size() >= 3)
        {
                from_string(tokens[2], blue);
                blue = (blue * 255) / 100;

                value = ((red << 16) & 0xFF0000) + ((green << 8) & 0x00FF00) + blue;

                emitChange();
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommand_cb(bool status, string command, string)
{
        if (!status)
        {
                cInfoDom("output") << "WODaliRVB::WagoUdpCommand(): Error with request " << command;

                return;
        }
}

void WODaliRVB::setColorReal(int r, int g, int b)
{
        string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                     " " + get_param("raddress") + " " + Utils::to_string((r * 100) / 255) +
                     " " + get_param("rfade_time");
        WagoMap::Instance(host, port).SendUDPCommand(cmd);

        cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
                     " " + get_param("gaddress") + " " + Utils::to_string((g * 100) / 255) +
                     " " + get_param("gfade_time");
        WagoMap::Instance(host, port).SendUDPCommand(cmd);

        cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
                     " " + get_param("baddress") + " " + Utils::to_string((b * 100) / 255) +
                     " " + get_param("bfade_time");
        WagoMap::Instance(host, port).SendUDPCommand(cmd);
}
