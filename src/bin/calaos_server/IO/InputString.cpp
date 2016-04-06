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
#include "InputString.h"

namespace Calaos {

InputString::InputString(Params &p):
    IOBase(p, IOBase::IO_INPUT)
{
    set_param("gui_type", "string_in");
    if (!get_params().Exists("visible")) set_param("visible", "true");

    if (get_params().Exists("interval")) Utils::from_string(get_param("interval"), frequency);
    if (get_params().Exists("frequency")) Utils::from_string(get_param("frequency"), frequency);

    cInfoDom("input") << get_param("id") << ": Ok";
}

InputString::~InputString()
{
    cInfoDom("input");
}

void InputString::emitChange()
{   
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", value } });

    EmitSignalIO();
}

std::string InputString::get_value_string()
{
    readValue();
    return value;
}

}
