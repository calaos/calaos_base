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
#include <WOVoletSmart.h>
#include <IPC.h>

using namespace Calaos;

WOVoletSmart::WOVoletSmart(Params &p):
                OutputShutterSmart(p),
                port(502)
{
        Utils::logger("output") << Priority::DEBUG << "WOVoletSmart::WOVoletSmart(" << get_param("id") << "): Ok" << log4cpp::eol;
}

WOVoletSmart::~WOVoletSmart()
{
        Utils::logger("output") << Priority::DEBUG << "WOVoletSmart::~WOVoletSmart(): Ok" << log4cpp::eol;
}

void WOVoletSmart::readConfig()
{
        host = get_param("host");
        if (get_params().Exists("port"))
                Utils::from_string(get_param("port"), port);
        Utils::from_string(get_param("var_up"), up_address);
        Utils::from_string(get_param("var_down"), down_address);
        OutputShutterSmart::readConfig();
}

void WOVoletSmart::setOutputUp(bool enable)
{
        readConfig();
        WagoMap::Instance(host, port).write_single_bit((UWord)up_address, enable, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
}

void WOVoletSmart::setOutputDown(bool enable)
{
        readConfig();
        WagoMap::Instance(host, port).write_single_bit((UWord)down_address, enable, sigc::mem_fun(*this, &WOVoletSmart::WagoWriteCallback));
}

void WOVoletSmart::WagoWriteCallback(bool status, UWord address, bool value)
{
        if (!status)
        {
                Utils::logger("output") << Priority::ERROR << "WOVoletSmart(" << get_param("id") << "): Failed to write value" << log4cpp::eol;
                return;
        }
}
