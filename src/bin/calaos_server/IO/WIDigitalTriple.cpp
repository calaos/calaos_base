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
#include <WIDigitalTriple.h>
#include <IPC.h>

using namespace Calaos;

WIDigitalTriple::WIDigitalTriple(Params &p):
                Input(p),
                port(502),
                value(0.0),
                count(0),
                timer(NULL)
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (!get_params().Exists("visible")) set_param("visible", "false");

        iter = Utils::signal_wago.connect( sigc::mem_fun(this, &WIDigitalTriple::ReceiveFromWago) );
        Utils::logger("input") << Priority::INFO << "WIDigitalTriple::WIDigitalTriple(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WIDigitalTriple::~WIDigitalTriple()
{
        iter->disconnect();
        if (timer) delete timer;
        Utils::logger("input") << Priority::INFO << "WIDigitalTriple::~WIDigitalTriple(): Ok" << log4cpp::eol;
}

void WIDigitalTriple::ReceiveFromWago(std::string ip, int addr, bool val, std::string intype)
{
        if (ip == host && addr == address)
        {
                if ((intype == "std" && get_param("knx") != "true") ||
                    (intype == "knx" && get_param("knx") == "true"))
                {
                        Utils::logger("input") << Priority::INFO << "WIDigitalTriple::ReceiveFromWago(): Got "
                                               << to_string(val) << " on " << intype << " input " << addr
                                               << log4cpp::eol;

                        udp_value = val;
                        hasChanged();
                }
        }
}

void WIDigitalTriple::WagoReadCallback(bool status, UWord address, int nb, vector<bool> &values)
{
        if (!status)
        {
                Utils::logger("input") << Priority::ERROR << "WIDigitalTriple(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                return;
        }
}

void WIDigitalTriple::hasChanged()
{
        bool val = false;

        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (get_param("knx") != "true")
        {
                //Force to reconnect in case of disconnection
                //WagoMap::Instance(host, port).read_bits((UWord)address, 1, sigc::mem_fun(*this, &WIDigitalTriple::WagoReadCallback));
        }

        val = udp_value;

        if (val)
        {
                if (!timer)
                {
                        count = 0;
                        timer = new EcoreTimer(0.5,
                                               (sigc::slot<void>)sigc::mem_fun(*this, &WIDigitalTriple::TimerDone));
                }

                count += 1;
        }
}

void WIDigitalTriple::TimerDone()
{
        if (count > 0)
        {
                if (count == 1) value = 1.;
                if (count == 2) value = 2.;
                if (count >= 3) value = 3.;

                count = 0;

                EmitSignalInput();

                string sig = "input ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode(string("state:") + to_string(value));
                IPC::Instance().SendEvent("events", sig);

                //reset input value to 0 after 250ms (simulate button press/release)
                EcoreTimer::singleShot(0.250, sigc::mem_fun(*this, &WIDigitalTriple::resetInput));
        }

        if (timer)
        {
                delete timer;
                timer = NULL;
        }
}

void WIDigitalTriple::resetInput()
{
        value = 0.;
}

