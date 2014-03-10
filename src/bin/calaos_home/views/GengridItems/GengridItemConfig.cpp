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

#include "GengridItemConfig.h"
#include <ApplicationMain.h>

GengridItemConfig::GengridItemConfig(Evas *_evas, Evas_Object *_parent, string _label, string style_addition, void *data):
    GengridItemBase(_evas, _parent,
                    "config" + string((style_addition != "")? "/" + style_addition:""),
                    data),
    label(_label)
{
}

GengridItemConfig::~GengridItemConfig()
{
}

string GengridItemConfig::getLabelItem(Evas_Object *obj, string part)
{
    return label;
}

