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
#include "xPLInputSwitch.h"
#include "xPLDocBase.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(xPLInputSwitch)

xPLInputSwitch::xPLInputSwitch(Params &p):
    InputSwitch(p),
    m_xPLValue(false)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("xPLInputSwitch");
    ioDoc->descriptionSet(_("xPL input switch"));
    xPLDocBase::commonSensorDoc(ioDoc);

    if(get_param("id")=="doc") return; //Json documentation detector

    string source = get_param("source");
    string sensor = get_param("sensor");

    xPLController::Instance().registerSensor(source, sensor, sigc::mem_fun(*this, &xPLInputSwitch::valueUpdated));

    cInfoDom("input") << source << ":" << sensor;
}

xPLInputSwitch::~xPLInputSwitch()
{
}

void xPLInputSwitch::valueUpdated(xPLInfoSensor *sensor)
{
    m_xPLValue = ((sensor->StringVal=="HIGH")||(sensor->StringVal=="high"));
    hasChanged();
}

bool xPLInputSwitch::readValue()
{
    return m_xPLValue;
}

