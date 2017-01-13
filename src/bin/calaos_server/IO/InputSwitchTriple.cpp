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
#include "InputSwitchTriple.h"

using namespace Calaos;

InputSwitchTriple::InputSwitchTriple(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    count(0),
    value(0.0),
    timer(NULL)
{
    ioDoc->descriptionBaseSet(_("Triple click switch. This switch can start 3 kind of actions. User has 500ms to do a multiple click."));
    ioDoc->paramAdd("visible", _("A switch can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);
    ioDoc->conditionAdd("1", _("Event triggered when switch is single clicked"));
    ioDoc->conditionAdd("2", _("Event triggered when switch is double clicked"));
    ioDoc->conditionAdd("3", _("Event triggered when switch is triple clicked"));
    ioDoc->conditionAdd("changed", _("Event on any change of state"));

    set_param("visible", "false");
    set_param("gui_type", "switch3");
}

InputSwitchTriple::~InputSwitchTriple()
{
    DELETE_NULL(timer);
}

void InputSwitchTriple::hasChanged()
{
    if (!isEnabled()) return;

    bool val = false;

    val = readValue();

    if (val)
    {
        if (!timer)
        {
            count = 0;
            timer = new Timer(0.5,
                                   (sigc::slot<void>)sigc::mem_fun(*this, &InputSwitchTriple::TimerDone));
        }

        count += 1;
    }
}

void InputSwitchTriple::TimerDone()
{
    if (count > 0)
    {
        if (count == 1) value = 1.;
        if (count == 2) value = 2.;
        if (count >= 3) value = 3.;

        count = 0;
        emitChange();
    }

    DELETE_NULL(timer);
}

void InputSwitchTriple::resetInput()
{
    value = 0.;
}

void InputSwitchTriple::emitChange()
{
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", Utils::to_string(value) } });

    //reset input value to 0 after 250ms (simulate button press/release)
    Timer::singleShot(0.250, sigc::mem_fun(*this, &InputSwitchTriple::resetInput));
}

bool InputSwitchTriple::set_value(double v)
{
    if (!isEnabled()) return false;

    value = v;
    emitChange();

    return true;
}

