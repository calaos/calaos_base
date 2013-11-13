/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
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
#include <InputSwitchLongPress.h>
#include <IPC.h>

using namespace Calaos;

InputSwitchLongPress::InputSwitchLongPress(Params &p):
                Input(p),
                value(0.0),
                timer(NULL)
{
        if (!get_params().Exists("visible")) set_param("visible", "false");

        set_param("gui_type", "switch_long");
}

InputSwitchLongPress::~InputSwitchLongPress()
{
       DELETE_NULL(timer);
}

void InputSwitchLongPress::hasChanged()
{
        bool val = readValue();

        if (val)
        {
                if (!timer)
                {
                        timer = new EcoreTimer(0.5, (sigc::slot<void>)sigc::mem_fun(*this, &InputSwitchLongPress::longPress_timer));
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
        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode(string("state:") + Utils::to_string(value));
        IPC::Instance().SendEvent("events", sig);

        //reset input value to 0 after 250ms (simulate button press/release)
        EcoreTimer::singleShot(0.250, sigc::mem_fun(*this, &InputSwitchLongPress::resetInput));
}

void InputSwitchLongPress::resetInput()
{
        value = 0.;
}

