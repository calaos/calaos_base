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
#include "InputSwitch.h"

using namespace Calaos;

InputSwitch::InputSwitch(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(false)
{
    ioDoc->descriptionBaseSet(_("Basic switch with press/release states."));
    ioDoc->paramAdd("visible", _("A switch can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);
    ioDoc->conditionAdd("true", _("Event triggered when switch is pressed"));
    ioDoc->conditionAdd("false", _("Event triggered when switch is released"));
    ioDoc->conditionAdd("changed", _("Event on any change of state"));

    set_param("visible", "false");
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
