/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
#include "Klf200InputSwitch.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(Klf200InputSwitch)

static const char *TAG = "klf200";

Klf200InputSwitch::Klf200InputSwitch(Params &p):
    InputSwitch(p)
{
    ioDoc->friendlyNameSet("Klf200InputSwitch");
    ioDoc->descriptionSet(_("Rain sensor of a VELUX window, read through a KLF 200 gateway. True while the sensor reports rain. The gateway is polled, so a change can take a few minutes to show."));
    ioDoc->linkAdd("VELUX KLF 200", "https://www.klf200.com/klf200");

    ioDoc->paramAdd("host", _("IP address or hostname of the KLF 200 gateway"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("password", _("Password of the KLF 200 gateway (the Wi-Fi password printed on the back of the unit)"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("node_id", _("Node ID of the window on the gateway (run calaos_klf200 --list to find it)"), 0, 255, true);

    if (!get_params().Exists("host") || get_param("host").empty())
    {
        cErrorDom(TAG) << "Klf200InputSwitch(" << get_param("id") << "): host parameter is required";
        return;
    }

    if (!get_params().Exists("password") || get_param("password").empty())
    {
        cErrorDom(TAG) << "Klf200InputSwitch(" << get_param("id") << "): password parameter is required";
        return;
    }

    if (!get_params().Exists("node_id") || !Utils::is_of_type<int>(get_param("node_id")))
    {
        cErrorDom(TAG) << "Klf200InputSwitch(" << get_param("id") << "): node_id parameter is required";
        return;
    }

    int nodeId;
    Utils::from_string(get_param("node_id"), nodeId);

    ctrl = &Klf200Ctrl::Instance();

    /* No lifetime guard in the callback: the destructor unregisters it, and
     * ListeRoom refuses to delete an IO while change callbacks run. */
    registrationId = ctrl->registerNode(Klf200Types::Host(get_param("host")),
                                        Klf200Types::Password(get_param("password")),
                                        Klf200Types::NodeId(nodeId),
                                        true,
                                        [this](const Params &event)
                                        {
        nodeEventReceived(event);
    });

    cInfoDom(TAG) << "Klf200InputSwitch(" << get_param("id") << "): rain sensor of node " << nodeId
                  << " on " << get_param("host");
} // LCOV_EXCL_LINE exception-cleanup landing pad only

Klf200InputSwitch::~Klf200InputSwitch()
{
    if (ctrl && registrationId != Klf200Registry::INVALID_ID)
        ctrl->unregisterNode(registrationId);
}

void Klf200InputSwitch::nodeEventReceived(const Params &event)
{
    if (event["event"] == "node")
    {
        if (event["rain_sensor"] == "false" && !warnedNoSensor)
        {
            warnedNoSensor = true;
            cWarningDom(TAG) << "Klf200InputSwitch(" << get_param("id") << "): node " << get_param("node_id")
                             << " (" << event["node_type"] << ") has no rain sensor, it will always read false";
        }
        return;
    }

    if (event["event"] != "rain")
        return;

    rain = event["rain"] == "true";
    hasChanged();
}

bool Klf200InputSwitch::readValue()
{
    return rain;
}
