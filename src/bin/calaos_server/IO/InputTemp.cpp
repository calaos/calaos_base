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
#include "InputTemp.h"
#include "ListeRule.h"
#include "CalaosConfig.h"

using namespace Calaos;

InputTemp::InputTemp(Params &p):
    InputAnalog(p)
{
    ioDoc->descriptionBaseSet(_("Temperature sensor input. Used for displaying temperature and to control heating devices with rules based on temperature value"));

    set_param("gui_type", "temp");
    set_param("io_style", "temperature");
}

InputTemp::~InputTemp()
{
}
