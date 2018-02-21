/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "xPLOutputSwitch.h"
#include "xPLDocBase.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(xPLOutputSwitch)

xPLOutputSwitch::xPLOutputSwitch(Params &p):
    OutputLight(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("xPLOutputSwitch");
    ioDoc->descriptionSet(_("Light/relay controlled by xPL Protocol"));
    xPLDocBase::commonActuatorDoc(ioDoc);

    if(get_param("id")=="doc") return; //Json documentation detector

    cInfoDom("output") << get_param("source") << ":" << get_param("actuator");
}

xPLOutputSwitch::~xPLOutputSwitch()
{
}

bool xPLOutputSwitch::set_value_real(bool val)
{
    string source = get_param("source");
    string actuator = get_param("actuator");
    string v;

    if(val == true)
      value = "high";
    else
      value = "low";

    xPLController::Instance().setValue(source, actuator, v);

    return true;
}
