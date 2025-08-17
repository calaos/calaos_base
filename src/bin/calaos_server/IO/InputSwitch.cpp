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
#include "InputSwitch.h"

using namespace Calaos;

InputSwitch::InputSwitch(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(false)
{
    ioDoc->descriptionBaseSet(_("Basic switch with press/release states."));
    ioDoc->paramAdd("visible", _("Display the Input/Output on all user interfaces if set. Default to false for switches"), IODoc::TYPE_BOOL, false, "false");
    ioDoc->conditionAdd("true", _("Event triggered when switch is pressed"));
    ioDoc->conditionAdd("false", _("Event triggered when switch is released"));
    ioDoc->conditionAdd("changed", _("Event on any change of state"));

    Params io_style = { { "switch", _("Switch") },
                        { "door", _("Door/Window sensor") },
                        { "occupancy", _("Occupancy sensor") },
                        { "smoke", _("Smoke detector") },
                        { "water", _("Water leak sensor") },
                        { "gas", _("Gas leak sensor") },
                        { "carbon_monoxide", _("Carbon monoxide sensor") },
                        { "sound", _("Sound sensor") },
                        { "motion", _("Motion sensor") },
                        { "vibration", _("Vibration sensor") },
                        { "lock", _("Lock sensor") },
                        { "garage_door", _("Garage door sensor") },
                       };
    ioDoc->paramAddList("io_style", _("GUI style display. This will control the icon displayed on the UI"), true, io_style, "switch");

    // Default style is switch
    if (!get_params().Exists("io_style"))
        set_param("io_style", "switch");

    // By default, the switch is not visible and other styles are visible
    if (!get_params().Exists("visible"))
    {
        if (get_param("io_style") == "switch")
            set_param("visible", "false");
        else
            set_param("visible", "true");
    }
    set_param("gui_type", "switch");
}

InputSwitch::~InputSwitch()
{
}

void InputSwitch::hasChanged()
{
    if (!isEnabled()) return;

    bool val = readValue();

    if (val != value)
    {
        value = val;
        emitChanges();
    }
}

void InputSwitch::emitChanges()
{
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", value?"true":"false" } });

    cInfoDom("input") << get_param("id") << ": " << value;
    EmitSignalIO();
}
