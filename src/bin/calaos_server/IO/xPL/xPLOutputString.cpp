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
#include "xPLOutputString.h"
#include "xPLDocBase.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(xPLOutputString)

xPLOutputString::xPLOutputString(Params &p):
    OutputString(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("xPLOutputAnalog");
    ioDoc->descriptionSet(_("Analog output controlled by xPL Protocol"));
    xPLDocBase::commonActuatorDoc(ioDoc);

    if(get_param("id")=="doc") return; //Json documentation detector

    cInfoDom("output") << get_param("source") << ":" << get_param("actuator");
}

xPLOutputString::~xPLOutputString()
{
}

void xPLOutputString::set_value_real(string val)
{
    string source = get_param("source");
    string actuator = get_param("actuator");

    xPLController::Instance().SetValue(source, actuator, val);
}