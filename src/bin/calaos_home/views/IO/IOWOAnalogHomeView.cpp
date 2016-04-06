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
#include "IOWOAnalogHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWOAnalogHomeView, More)
ITEM_BUTTON_CALLBACK(IOWOAnalogHomeView, Less)

IOWOAnalogHomeView::IOWOAnalogHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, std::string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, std::string("WOAnalog_") + style_addition, _flags),
    IOBaseElement(_io)
{
}

IOWOAnalogHomeView::~IOWOAnalogHomeView()
{
}

void IOWOAnalogHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWOAnalogHomeView::getPartItem(Evas_Object *obj, std::string part)
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
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Less, this);
    }

    initView();

    return o;
}

std::string IOWOAnalogHomeView::getLabelItem(Evas_Object *obj, std::string part)
{
    std::string text;

    if (!io) return text;

    if (part == "text")
        text = io->params["name"];
    else if (part == "item.value")
        text = io->params["state"] + " " + io->params["unit"];

    return text;
}

void IOWOAnalogHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOWOAnalogHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "item.value", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOWOAnalogHomeView::buttonClickMore()
{
    if (!io) return;

    io->sendAction("inc");
}

void IOWOAnalogHomeView::buttonClickLess()
{
    if (!io) return;

    io->sendAction("dec");
}
