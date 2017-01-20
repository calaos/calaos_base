/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "WOLOutputBool.h"
#include "IOFactory.h"
#include "uvw/src/uvw.hpp"

using namespace Calaos;

REGISTER_IO(WOLOutputBool)

WOLOutputBool::WOLOutputBool(Params &p):
    IOBase(p, IOBase::IO_OUTPUT)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WOLOutputBool");
    ioDoc->descriptionSet(_("Wake On Lan output object. Send wake-on-lan packet to a device on the network."));
    ioDoc->paramAdd("address", _("Ethernet MAC address of the host to wake up"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("interval", _("Interval between pings in ms. Default to 15 sec"), IODoc::TYPE_INT, false, "15000");
    ioDoc->actionAdd("true", _("Send wake on lan packet to the configured device"));

    if (!param_exists("interval")) set_param("interval", "15000");

    //Not visible by default
    if (!get_params().Exists("visible")) set_param("visible", "false");

    set_param("gui_type", "var_bool");
}

WOLOutputBool::~WOLOutputBool()
{
}

bool WOLOutputBool::set_value(bool val)
{
    if (!isEnabled()) return true;

    if (val)
        doWakeOnLan();

    return true;
}

bool WOLOutputBool::set_value(string val)
{
    return set_value(val == "true");
}

void WOLOutputBool::doWakeOnLan()
{
    //Decode MAC address

    auto parseHex = [](char c)
    {
        int h = -1;
        if (c >= '0' && c <= '9') h = c - '0';
        if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
        return h;
    };

    string addr = get_param("address");
    Utils::replace_str(addr, ":", "");
    Utils::replace_str(addr, "-", "");
    Utils::replace_str(addr, ".", "");

    bool err = false;
    for (uint i = 0;i < addr.length();i++)
    {
        char c = addr[i];
        if (c >= '0' && c <= '9') continue;
        if (c >= 'a' && c <= 'f') continue;

        err = true;
        break;
    }

    if (addr.length() != 12 || err)
    {
        cErrorDom("output") << "WakeOnLan: Wrong MAC address: " << addr;
        return;
    }

    vector<uint8_t> address;
    for (int i = 0;i < 12;i += 2)
        address.push_back((parseHex(addr[i]) << 4) | uint8_t(parseHex(addr[i + 1])));

    vector<uint8_t> magicPacket;
    magicPacket.reserve(6 + 6 * 16);

    // First 6bytes, 0xFF
    for (int i = 0;i < 6;i++)
        magicPacket.push_back(0xFF);

    // 16 times mac address
    for (int i = 0;i < 16;i++)
        magicPacket.insert(magicPacket.end(), address.begin(), address.end());

    auto loop = uvw::Loop::getDefault();
    auto udp_con = loop->resource<uvw::UDPHandle>();

    udp_con->broadcast(true);
    udp_con->send("255.255.255.255", 7, (char *)magicPacket.data(), magicPacket.size());
    udp_con->once<uvw::SendEvent>([](auto &, auto &h)
    {
        h.close();
    });

    EmitSignalIO();
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", "true" } });

    if (timerState)
        timerState->Reset();
    else
        timerState = new Timer(1.5, sigc::mem_fun(*this, &WOLOutputBool::timerTimeout));
}

void WOLOutputBool::timerTimeout()
{
    EmitSignalIO();
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", "false" } });

    DELETE_NULL(timerState);
}
