/******************************************************************************
**  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef xPLDocBase_H
#define xPLDocBase_H

#include "IODoc.h"

class xPLDocBase
{
public:
    static void commonSensorDoc(IODoc *ioDoc);
    static void commonActuatorDoc(IODoc *ioDoc);

private:
    static void commonDoc(IODoc *ioDoc);
};

#endif
