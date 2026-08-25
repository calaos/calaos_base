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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ListeRule.h"
#include "ReolinkInputSwitch.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(ReolinkInputSwitch)

ReolinkInputSwitch::ReolinkInputSwitch(Params &p):
    InputSwitch(p),
    eventReceived(false)
{
    ioDoc->friendlyNameSet("ReolinkInputSwitch");
    ioDoc->descriptionSet(_("Switch activated by events from a Reolink camera"));

    ioDoc->paramAdd("hostname", _("IP address or hostname of the Reolink camera"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("username", _("Username for authentication with Reolink camera"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("password", _("Password for authentication with Reolink camera"), IODoc::TYPE_STRING, true);

    Params event_types;
    event_types.Add("motion", _("Motion Detection"));
    event_types.Add("person", _("Person Detection"));
    event_types.Add("vehicle", _("Vehicle Detection"));
    event_types.Add("pet", _("Pet Detection"));
    event_types.Add("cry", _("Cry Detection"));
    event_types.Add("face", _("Face Detection"));
    event_types.Add("package", _("Package Detection"));
    event_types.Add("visitor", _("Visitor/Doorbell Detection"));

    ioDoc->paramAddList("event_type", _("Type of event to listen for from the Reolink camera"), true, event_types, "motion");

    if (!get_params().Exists("hostname") || get_param("hostname").empty())
    {
        cErrorDom("reolink") << "ReolinkInputSwitch: hostname parameter is required";
        return;
    }

    if (!get_params().Exists("username") || get_param("username").empty())
    {
        cErrorDom("reolink") << "ReolinkInputSwitch: username parameter is required";
        return;
    }

    if (!get_params().Exists("password") || get_param("password").empty())
    {
        cErrorDom("reolink") << "ReolinkInputSwitch: password parameter is required";
        return;
    }

    if (!get_params().Exists("event_type") || get_param("event_type").empty())
    {
        cErrorDom("reolink") << "ReolinkInputSwitch: event_type parameter is required";
        return;
    }

    // Use the singleton instance
    ctrl = &ReolinkCtrl::Instance();

    string hostname = get_param("hostname");
    string username = get_param("username");
    string password = get_param("password");
    string event_type = get_param("event_type");

    cDebugDom("reolink") << "Registering camera " << hostname << " for event " << event_type;

    // The callback is stored in the long-lived ReolinkCtrl singleton: guard
    // it with the alive token (belt) and remove it in the destructor (braces)
    /* T3.31 - each value is named on its own line, next to the field it
     * fills. Handing them over in any other order no longer compiles. */
    registrationId = ctrl->registerCamera(ReolinkTypes::Hostname(hostname),
                        ReolinkTypes::Username(username),
                        ReolinkTypes::Password(password),
                        ReolinkTypes::EventType(event_type),
                        [this, weakAlive = std::weak_ptr<bool>(alive)](const ReolinkTypes::Hostname &host,
                                                                       const ReolinkTypes::EventType &evt_type,
                                                                       const ReolinkTypes::EventData &evt_data)
                        {
                            if (weakAlive.expired())
                                return; //IO deleted, stale callback
                            eventReceivedCallback(host, evt_type, evt_data);
                        });

    cInfoDom("input") << "ReolinkInputSwitch created for camera " << hostname << " event " << event_type;
}

ReolinkInputSwitch::~ReolinkInputSwitch()
{
    // Remove our callback from the ReolinkCtrl singleton, otherwise it
    // dangles and fires on freed memory at the next camera event
    if (ctrl && registrationId != ReolinkEventRegistry::INVALID_ID)
        ctrl->unregisterCamera(registrationId);
}

void ReolinkInputSwitch::eventReceivedCallback(const ReolinkTypes::Hostname &hostname,
                                               const ReolinkTypes::EventType &event_type,
                                               const ReolinkTypes::EventData &event_data)
{
    cDebugDom("reolink") << "Event received from " << hostname.v << " type: " << event_type.v << " data: " << event_data.v;

    lastEventData = event_data.v;
    eventReceived = true;

    // Trigger value change detection
    hasChanged();

    // Reset the value to false after a short delay.
    // Guard with the alive token: the timer may fire after this IO is deleted
    Timer::singleShot(0.250, [this, weakAlive = std::weak_ptr<bool>(alive)]()
    {
        if (weakAlive.expired())
            return; //IO deleted before the timer fired
        value = false;
    });
}

bool ReolinkInputSwitch::readValue()
{
    if (!eventReceived)
    {
        cDebugDom("reolink") << "No event received yet";
        return false;
    }

    cDebugDom("reolink") << "Event data: " << lastEventData;

    // For most Reolink events, we consider any event as a "true" state
    // and then automatically reset to false after a short time
    // This makes it behave like a momentary switch

    // Reset the event flag after reading
    eventReceived = false;

    return true;
}