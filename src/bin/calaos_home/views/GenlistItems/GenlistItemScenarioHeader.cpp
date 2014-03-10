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
#include "GenlistItemScenarioHeader.h"
#include <ApplicationMain.h>

GenlistItemScenarioHeader::GenlistItemScenarioHeader(Evas *_evas, Evas_Object *_parent, string _title):
    GenlistItemBase(_evas, _parent, "scenario/header", ELM_GENLIST_ITEM_GROUP),
    title(_title)
{
}

GenlistItemScenarioHeader::~GenlistItemScenarioHeader()
{
}

void GenlistItemScenarioHeader::itemAdded()
{
    elm_genlist_item_select_mode_set(item, ELM_OBJECT_SELECT_MODE_DISPLAY_ONLY);
}

Evas_Object *GenlistItemScenarioHeader::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    return o;
}

string GenlistItemScenarioHeader::getLabelItem(Evas_Object *obj, string part)
{
    if (part == "elm.text")
        return title;

    return "";
}
