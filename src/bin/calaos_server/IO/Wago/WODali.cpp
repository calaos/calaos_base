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
#include <WODali.h>
#include <IPC.h>

using namespace Calaos;

WODali::WODali(Params &_p):
                OutputLightDimmer(_p),
                port(502)
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "true");

        string cmd = "WAGO_DALI_GET " + get_param("line") + " " + get_param("address");
        WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODali::WagoUDPCommand_cb));

        Calaos::StartReadRules::Instance().addIO();
        cDebugDom("output") << "WODali::WODali(" << get_param("id") << "): Ok";
}

WODali::~WODali()
{
        cDebugDom("output") << "WODali::WODali(): Ok";
}

bool WODali::set_value_real(int val)
{
        string cmd = "WAGO_DALI_SET " + get_param("line") + " " + get_param("group") +
                     " " + get_param("address") + " " + Utils::to_string(val) +
                     " " + get_param("fade_time");
        WagoMap::Instance(host, port).SendUDPCommand(cmd);

        return true;
}

void WODali::WagoUDPCommand_cb(bool status, string command, string result)
{
        if (!status)
        {
                cInfoDom("output") << "WODali::WagoUdpCommand(): Error with request " << command;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (command.find("WAGO_DALI_GET") != string::npos)
        {
                vector<string> tokens;
                split(result, tokens);
                if (tokens.size() >= 3)
                {
                        //get the status of the ballast
                        if (tokens[1] == "0")
                        {
                                value = 0;
                                old_value = 100;
                        }
                        else
                        {
                                value = 100;
                                old_value = 0;
                        }
                }

                emitChange();
        }

        Calaos::StartReadRules::Instance().ioRead();
}
