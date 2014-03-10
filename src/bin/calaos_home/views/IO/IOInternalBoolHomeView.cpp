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
#include "IOInternalBoolHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOInternalBoolHomeView, On)
ITEM_BUTTON_CALLBACK(IOInternalBoolHomeView, Off)

IOInternalBoolHomeView::IOInternalBoolHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("InternalBool_") + style_addition + ((_io->params["rw"] == "true")?"/rw":""), _flags),
    IOBaseElement(_io)
{
}

IOInternalBoolHomeView::~IOInternalBoolHomeView()
{
}

void IOInternalBoolHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOInternalBoolHomeView::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    if (part == "calaos.button.on")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/select");
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_On, this);
    }
    else if (part == "calaos.button.off")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/unselect");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Off, this);
    }

    initView();

    return o;
}

string IOInternalBoolHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
        text = io->params["name"];

    return text;
}

void IOInternalBoolHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

    if (io->params["state"] == "true")
    {
        itemEmitSignal("text,active,blue", "calaos");
        itemEmitSignal("on,normal", "calaos");
    }
    else
    {
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,normal", "calaos");
    }
}

void IOInternalBoolHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

    if (io->params["state"] == "true")
    {
        itemEmitSignal("text,active,blue", "calaos");
        itemEmitSignal("on,anim", "calaos");
    }
    else
    {
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,anim", "calaos");
    }
}

void IOInternalBoolHomeView::buttonClickOn()
{
    if (!io) return;

    io->sendAction("true");
}

void IOInternalBoolHomeView::buttonClickOff()
{
    if (!io) return;

    io->sendAction("false");
}
