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
#include "IOWITempHomeView.h"
#include <ApplicationMain.h>

IOWITempHomeView::IOWITempHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, std::string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, std::string("WITemp_") + style_addition, _flags),
    IOBaseElement(_io)
{
}

IOWITempHomeView::~IOWITempHomeView()
{
}

void IOWITempHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWITempHomeView::getPartItem(Evas_Object *obj, std::string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    initView();

    return o;
}

std::string IOWITempHomeView::getLabelItem(Evas_Object *obj, std::string part)
{
    std::string text;

    if (!io) return text;

    if (part == "text")
    {
        text = io->params["name"];
    }
    else if (part == "item.value")
    {
        text = io->params["state"] + " °C";

        IOBase *consigne = CalaosModel::Instance().getHome()->getConsigneFromTemp(io);
        if (consigne)
            text += "<consigne> / " + consigne->params["state"] + " °C</consigne>";
    }

    return text;
}

void IOWITempHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOWITempHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}
