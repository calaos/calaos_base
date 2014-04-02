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
#include <WIDigitalLong.h>
#include <WagoMap.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_INPUT(WIDigitalLong)
REGISTER_INPUT_USERTYPE(WagoInputSwitchLongPress, WIDigitalLong)

WIDigitalLong::WIDigitalLong(Params &p):
    InputSwitchLongPress(p),
    port(502)
{
    host = get_param("host");
    Utils::from_string(get_param("var"), address);
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port);

    iter = Utils::signal_wago.connect( sigc::mem_fun(this, &WIDigitalLong::ReceiveFromWago) );
    cDebugDom("input") << get_param("id") << ": Ok";
}

WIDigitalLong::~WIDigitalLong()
{
    iter->disconnect();
    cDebugDom("input");
}

void WIDigitalLong::ReceiveFromWago(std::string ip, int addr, bool val, std::string intype)
{
    if (ip == host && addr == address)
    {
        if ((intype == "std" && get_param("knx") != "true") ||
            (intype == "knx" && get_param("knx") == "true"))
        {
            cInfoDom("input") << "Got "
                              << Utils::to_string(val) << " on " << intype << " input " << addr;

            udp_value = val;
            hasChanged();
        }
    }
}

void WIDigitalLong::WagoReadCallback(bool status, UWord addr, int nb, vector<bool> &values)
{
    if (!status)
    {
        cErrorDom("input") << get_param("id") << ": Failed to read value";
        return;
    }
}

bool WIDigitalLong::readValue()
{
    host = get_param("host");
    Utils::from_string(get_param("var"), address);
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    if (get_param("knx") != "true")
    {
        //Force to reconnect in case of disconnection
        //WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalLong::WagoReadCallback));
    }

    return udp_value;
}

