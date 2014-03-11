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
#include <WIAnalog.h>

using namespace Calaos;

WIAnalog::WIAnalog(Params &p):
    InputAnalog(p),
    port(502),
    requestInProgress(false),
    start(true)
{
    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    Utils::from_string(get_param("var"), address);

    WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WIAnalog::WagoReadCallback));
    requestInProgress = true;

    cDebugDom("input") << get_param("id") << ": Ok";
}

WIAnalog::~WIAnalog()
{
    cDebugDom("input");
}

void WIAnalog::WagoReadCallback(bool status, UWord addr, int count, vector<UWord> &values)
{
    requestInProgress = false;

    if (!status)
    {
        cErrorDom("input") << get_param("id") << ": Failed to read value";
        if (start)
        {
            Calaos::StartReadRules::Instance().ioRead();
            start = false;
        }

        return;
    }

    double val = value;

    if (!values.empty())
    {
        val = values[0];
    }

    if (val != value)
    {
        value = val;
        emitChange();
    }

    if (start)
    {
        Calaos::StartReadRules::Instance().ioRead();
        start = false;
    }
}

void WIAnalog::readValue()
{
    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    Utils::from_string(get_param("var"), address);

    if (!requestInProgress)
    {
        requestInProgress = true;
        WagoMap::Instance(host, port).read_words((UWord)address, 1, sigc::mem_fun(*this, &WIAnalog::WagoReadCallback));
    }
}
