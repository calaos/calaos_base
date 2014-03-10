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
#include "IOWIAnalogHomeView.h"
#include <ApplicationMain.h>

IOWIAnalogHomeView::IOWIAnalogHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("WIAnalog_") + style_addition, _flags),
    IOBaseElement(_io)
{
}

IOWIAnalogHomeView::~IOWIAnalogHomeView()
{
}

void IOWIAnalogHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWIAnalogHomeView::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    initView();

    return o;
}

string IOWIAnalogHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
        text = io->params["name"];
    else if (part == "item.value")
        text = io->params["state"] + " " + io->params["unit"];

    return text;
}

void IOWIAnalogHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOWIAnalogHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}
