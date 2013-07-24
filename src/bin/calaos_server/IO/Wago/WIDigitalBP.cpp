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
#include <IPC.h>

using namespace Calaos;

WIDigitalBP::WIDigitalBP(Params &p):
                Input(p),
                port(502),
                value(false),
                initial(true)
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "false");

        iter = Utils::signal_wago.connect( sigc::mem_fun(this, &WIDigitalBP::ReceiveFromWago) );

        if (get_param("knx") != "true")
        {
                WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBP::WagoReadCallback));

                Calaos::StartReadRules::Instance().addIO();
        }
        else
        {
                Utils::logger("input") << Priority::INFO << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Not reading initial state for KNX inputs" << log4cpp::eol;
        }

        Utils::logger("input") << Priority::INFO << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WIDigitalBP::~WIDigitalBP()
{
        iter->disconnect();
        Utils::logger("input") << Priority::INFO << "WIDigitalBP::~WIDigitalBP(): Ok" << log4cpp::eol;
}

void WIDigitalBP::ReceiveFromWago(std::string ip, int addr, bool val, std::string intype)
{
        if (ip == host && addr == address)
        {
                if ((intype == "std" && get_param("knx") != "true") ||
                    (intype == "knx" && get_param("knx") == "true"))
                {
                        Utils::logger("input") << Priority::INFO << "WIDigitalBP::ReceiveFromWago(): Got "
                                               << to_string(val) << " on " << intype << " input " << addr
                                               << log4cpp::eol;

                        udp_value = val;
                        hasChanged();
                }
        }
}

void WIDigitalBP::WagoReadCallback(bool status, UWord addr, int count, vector<bool> &values)
{
        if (!status)
        {
                Utils::logger("input") << Priority::ERROR << "WIDigitalBP(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                Calaos::StartReadRules::Instance().ioRead();

                return;
        }

        if (initial)
        {
                if (!values.empty())
                        value = values[0];

                if (value)
                        Utils::logger("input") << Priority::INFO << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Reading initial state: true" << log4cpp::eol;
                else
                        Utils::logger("input") << Priority::INFO << "WIDigitalBP::WIDigitalBP(" << get_param("id") << "): Reading initial state: false" << log4cpp::eol;
                initial = false;
        }

        Calaos::StartReadRules::Instance().ioRead();
}

void WIDigitalBP::hasChanged()
{
        bool val = false;
        std::vector<bool> values;

        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (get_param("knx") != "true")
        {
                //Force to reconnect in case of disconnection
                WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalBP::WagoReadCallback));
        }

        val = udp_value;

        if (val != value)
        {
                value = val;

                string sig = "input ";
                sig += get_param("id") + " ";
                if (value)
                        sig += Utils::url_encode(string("state:true"));
                else
                        sig += Utils::url_encode(string("state:false"));
                IPC::Instance().SendEvent("events", sig);

                EmitSignalInput();
        }
}
