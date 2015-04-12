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
#include <InputSwitch.h>
#include <IPC.h>

using namespace Calaos;

InputSwitch::InputSwitch(Params &p):
    Input(p),
    value(false)
{
    if (!get_params().Exists("visible")) set_param("visible", "false");

    set_param("gui_type", "switch");
}

InputSwitch::~InputSwitch()
{
}

void InputSwitch::hasChanged()
{
    bool val = readValue();

    if (val != value)
    {
        value = val;
        emitChanges();
    }
}

void InputSwitch::emitChanges()
{
    string sig = "input ";
    sig += get_param("id") + " ";
    if (value)
        sig += Utils::url_encode(string("state:true"));
    else
        sig += Utils::url_encode(string("state:false"));
    IPC::Instance().SendEvent("events", sig);

    cInfoDom("input") << get_param("id") << ": " << value;
    EmitSignalInput();

}
