/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "IOInternalIntHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOInternalIntHomeView, More)
ITEM_BUTTON_CALLBACK(IOInternalIntHomeView, Less)

IOInternalIntHomeView::IOInternalIntHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("InternalInt_") + style_addition + ((_io->params["rw"] == "true")?"/rw":""), _flags),
    IOBaseElement(_io)
{
}

IOInternalIntHomeView::~IOInternalIntHomeView()
{
}

void IOInternalIntHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOInternalIntHomeView::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    if (part == "calaos.button.plus")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/more");
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_More, this);
    }
    else if (part == "calaos.button.moins")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/less");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Less, this);
    }

    initView();

    return o;
}

string IOInternalIntHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
        text = io->params["name"];
    if (part == "item.value")
        text = io->params["state"];

    return text;
}

void IOInternalIntHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOInternalIntHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOInternalIntHomeView::buttonClickMore()
{
    if (!io) return;

    io->sendAction("inc");
}

void IOInternalIntHomeView::buttonClickLess()
{
    if (!io) return;

    io->sendAction("dec");
}
