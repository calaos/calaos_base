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
#include "OutputFake.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(OutputFake)

OutputFake::OutputFake(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    value(false)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("OutputFake");
    ioDoc->descriptionSet(_("Fake test output. Do nothing. Do not use."));

    cInfoDom("output") << "OutputFake::OutputFake(" << get_param("id") << "): Ok";

    set_param("visible", "false");
}

OutputFake::~OutputFake()
{
    cInfoDom("output") << "OutputFake::~OutputFake(): Ok";
}

bool OutputFake::set_value(bool val)
{
    if (!isEnabled()) return true;

    value = val;

    cInfoDom("output") << "OutputFake(" << get_param("id") << "): got action, " << ((value)?"True":"False");

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", val?"true":"false" } });

    EmitSignalIO();

    return true;
}
