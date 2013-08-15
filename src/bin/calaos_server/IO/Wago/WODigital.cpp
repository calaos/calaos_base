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
#include <WODigital.h>

using namespace Calaos;

WODigital::WODigital(Params &p):
                OutputLight(p),
                port(502),
                start(true)
{
        host = get_param("host");

        from_string(get_param("var"), address);

        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        if (get_param("knx") == "true")
                address += WAGO_KNX_START_ADDRESS;

        //Do this before translating address to 841/849
        WagoMap::Instance(host, port).read_output_bits((UWord)address, 1, sigc::mem_fun(*this, &WODigital::WagoReadCallback));

        if (get_param("wago_841") == "true" && get_param("knx") != "true")
                address += WAGO_841_START_ADDRESS;

        Calaos::StartReadRules::Instance().addIO();

        Utils::logger("output") << Priority::DEBUG << "WODigital::WODigital(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WODigital::~WODigital()
{
        Utils::logger("output") << Priority::DEBUG << "WODigital::~WODigital(): Ok" << log4cpp::eol;
}

void WODigital::WagoReadCallback(bool status, UWord addr, int count, vector<bool> &values)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WODigital(" << get_param("id") << "): Failed to read value" << log4cpp::eol;
                if (start)
                {
                    Calaos::StartReadRules::Instance().ioRead();
                    start = false;
                }

                return;
        }

        if (!values.empty())
                value = values[0];

        Utils::logger("output") << Priority::INFO << "WODigital(" << get_param("id") << "): Reading initial value: " << (value?"true":"false") << log4cpp::eol;

        emitChange();

        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }
}

void WODigital::WagoWriteCallback(bool status, UWord addr, bool _value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WODigital(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }
}

bool WODigital::set_value_real(bool val)
{
        host = get_param("host");
        Utils::from_string(get_param("var"), address);
        if (get_param("knx") == "true")
                address += WAGO_KNX_START_ADDRESS;
        if (get_param("wago_841") == "true" && get_param("knx") != "true")
                address += WAGO_841_START_ADDRESS;
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);

        WagoMap::Instance(host, port).write_single_bit((UWord)address, val, sigc::mem_fun(*this, &WODigital::WagoWriteCallback));

        return true;
}

