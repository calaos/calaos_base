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
#include "InputSwitchLongPress.h"

using namespace Calaos;

InputSwitchLongPress::InputSwitchLongPress(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(0.0),
    timer(NULL)
{
    ioDoc->descriptionBaseSet(_("Long press switch. This switch supports single press and long press. User has 500ms to perform the long press."));
    ioDoc->paramAdd("visible", _("A switch can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);
    ioDoc->conditionAdd("1", _("Event triggered when switch is pressed quickly"));
    ioDoc->conditionAdd("2", _("Event triggered when switch is pressed at least for 500ms (long press)"));
    ioDoc->conditionAdd("changed", _("Event on any change of state"));

    set_param("visible", "false");
    set_param("gui_type", "switch_long");
}

InputSwitchLongPress::~InputSwitchLongPress()
{
    DELETE_NULL(timer);
}

void InputSwitchLongPress::hasChanged()
{
    if (!isEnabled()) return;

    bool val = readValue();

    if (val)
    {
        if (!timer)
        {
            timer = new Timer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &InputSwitchLongPress::longPress_timer));
        }
    }
    else
    {
        if (timer)
        {
            DELETE_NULL(timer);

            value = 1.; // standard action
            emitChange();
        }
    }
}

void InputSwitchLongPress::longPress_timer()
{
    DELETE_NULL(timer);

    value = 2.; // long press action
    emitChange();
}

void InputSwitchLongPress::emitChange()
{
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(value) } });

    //reset input value to 0 after 250ms (simulate button press/release)
    Timer::singleShot(0.250, [=]()
    {
        value = 0;
    });
}

bool InputSwitchLongPress::set_value(double v)
{
    if (!isEnabled()) return false;

    value = v;
    emitChange();

    return true;
}
