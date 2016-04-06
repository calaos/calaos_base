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
#include "IOWODigitalHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWODigitalHomeView, On)
ITEM_BUTTON_CALLBACK(IOWODigitalHomeView, Off)

IOWODigitalHomeView::IOWODigitalHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, std::string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, std::string("WODigital_") + style_addition, _flags),
    IOBaseElement(_io)
{
}

IOWODigitalHomeView::~IOWODigitalHomeView()
{
}

void IOWODigitalHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWODigitalHomeView::getPartItem(Evas_Object *obj, std::string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    if (part == "calaos.button.on")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/light_on");
        elm_object_style_set(o, "calaos/action_button/yellow");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_On, this);
    }
    else if (part == "calaos.button.off")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/light_off");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Off, this);
    }

    initView();

    return o;
}

std::string IOWODigitalHomeView::getLabelItem(Evas_Object *obj, std::string part)
{
    std::string text;

    if (!io) return text;

    if (part == "text")
        text = io->params["name"];

    return text;
}

void IOWODigitalHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

    if (io->params["state"] == "true")
    {
        itemEmitSignal("text,active,yellow", "calaos");
        itemEmitSignal("on,normal", "calaos");
    }
    else
    {
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,normal", "calaos");
    }
}

void IOWODigitalHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

    if (io->params["state"] == "true")
    {
        itemEmitSignal("text,active,yellow", "calaos");
        itemEmitSignal("on,anim", "calaos");
    }
    else
    {
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,anim", "calaos");
    }
}

void IOWODigitalHomeView::buttonClickOn()
{
    if (!io) return;

    io->sendAction("true");
}

void IOWODigitalHomeView::buttonClickOff()
{
    if (!io) return;

    io->sendAction("false");
}
