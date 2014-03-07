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
#include <WIDigitalBP.h>

using namespace Calaos;

WIDigitalBP::WIDigitalBP(Params &p):
                InputSwitch(p),
                port(502),
                initial(true)
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        iter = Utils::signal_wago.connect( sigc::mem_fun(this, &WIDigitalBP::ReceiveFromWago) );

        if (get_param("knx") != "true")
        {
                WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBP::WagoReadCallback));

                Calaos::StartReadRules::Instance().addIO();
        }
        else
        {
                cInfoDom("input") << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Not reading initial state for KNX inputs";
        }

        cDebugDom("input") << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Ok";
}

WIDigitalBP::~WIDigitalBP()
{
        iter->disconnect();
        cDebugDom("input") << "WIDigitalBP::~WIDigitalBP(): Ok";
}

void WIDigitalBP::ReceiveFromWago(std::string ip, int addr, bool val, std::string intype)
{
        if (ip == host && addr == address)
        {
                if ((intype == "std" && get_param("knx") != "true") ||
                    (intype == "knx" && get_param("knx") == "true"))
                {
                        cInfoDom("input") << "WIDigitalBP::ReceiveFromWago(): Got "
                                               << Utils::to_string(val) << " on " << intype << " input " << addr
                                              ;

                        udp_value = val;
                        hasChanged();
                }
        }
}

void WIDigitalBP::WagoReadCallback(bool status, UWord addr, int count, vector<bool> &values)
{
        if (!status)
        {
                cErrorDom("input") << "WIDigitalBP(" << get_param("id") << "): Failed to read value";
                if (initial)
                {
                        Calaos::StartReadRules::Instance().ioRead();
                        initial = false;
                }

                return;
        }

        if (initial)
        {
                if (!values.empty())
                        value = values[0];

                if (value)
                        cInfoDom("input") << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Reading initial state: true";
                else
                        cInfoDom("input") << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Reading initial state: false";
                initial = false;

                Calaos::StartReadRules::Instance().ioRead();
        }

}

bool WIDigitalBP::readValue()
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (get_param("knx") != "true")
        {
                //Force to reconnect in case of disconnection
                WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBP::WagoReadCallback));
        }

        return udp_value;
}

