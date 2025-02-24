/******************************************************************************
**  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "xPLDocBase.h"

void xPLDocBase::commonSensorDoc(IODoc *ioDoc)
{
    commonDoc(ioDoc);
    ioDoc->paramAdd("sensor", _("Sensor ID, as set in your xPL network"), IODoc::TYPE_STRING, true);
}

void xPLDocBase::commonActuatorDoc(IODoc *ioDoc)
{
    commonDoc(ioDoc);
    ioDoc->paramAdd("actuator", _("Actuator ID, as set in your xPL network"), IODoc::TYPE_STRING, true);
}

void xPLDocBase::commonDoc(IODoc *ioDoc)
{
    ioDoc->paramAdd("source", _("Source name, as set in your xPL network (Format VendorId-DeviceId.Instance)"), IODoc::TYPE_STRING, true);
}
