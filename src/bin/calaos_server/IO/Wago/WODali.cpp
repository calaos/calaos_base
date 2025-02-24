/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include <WODali.h>
#include <WagoMap.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(WODali)
REGISTER_IO_USERTYPE(WagoOutputDimmer, WODali)

WODali::WODali(Params &_p):
    OutputLightDimmer(_p),
    port(502)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WODali");
    ioDoc->aliasAdd("WagoOutputDimmer");
    ioDoc->descriptionSet(_("Light using DALI or DMX. For DALI you need a 750-641 wago module. For DMX, a DMX4ALL-LAN device connected to the Wago PLC."));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/750-641"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/dmx-lan"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Wago ethernet port, default to 502"), 0, 65535, false, 502);
    ioDoc->paramAdd("line", _("DALI bus line, usually 1"), IODoc::TYPE_INT, false, "1");
    ioDoc->paramAddInt("address", _("Device address. For DALI address is between 1-64. "
                                 "For DMX, the address starts at 100. So for DMX device 5, address should be 105"), 1, 612, true);
    ioDoc->paramAddInt("group", _("Set to 1 if address is a DALI group address, set to 0 otherwise."), 0, 1, false);
    ioDoc->paramAddInt("fade_time", _("DALI fade time. value is between 1-10"), 1, 10, false);

    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port);

    if (!get_params().Exists("visible")) set_param("visible", "true");
    if (!get_params().Exists("line")) set_param("line", "1");
    if (!get_params().Exists("fade_time")) set_param("fade_time", "1");

    string cmd = "WAGO_DALI_GET " + get_param("line") + " " + get_param("address");
    WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODali::WagoUDPCommand_cb));

    Calaos::StartReadRules::Instance().addIO();
    cDebugDom("output") << get_param("id") << ": Ok";
}

WODali::~WODali()
{
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
        cInfoDom("output") << "Error with request " << command;
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
