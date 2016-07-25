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
#include "OutputString.h"

using namespace Calaos;
using namespace Utils;

OutputString::OutputString(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    value("")
{
    set_param("gui_type", "string_out");

    readConfig();

    cInfoDom("output") << get_param("id") << ": Ok";
}

OutputString::~OutputString()
{
}

void OutputString::readConfig()
{
    if (!get_params().Exists("visible")) set_param("visible", "true");
}

void OutputString::emitChange()
{   
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", value } });
}

bool OutputString::set_value(string val)
{
    if (!isEnabled()) return true;

    readConfig();

    set_value_real(val);
   
    value = val;
    EmitSignalIO();
    emitChange();

    return true;
}

