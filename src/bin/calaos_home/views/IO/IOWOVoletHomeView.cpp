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
#include "IOWOVoletHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWOVoletHomeView, Up)
ITEM_BUTTON_CALLBACK(IOWOVoletHomeView, Down)
ITEM_BUTTON_CALLBACK(IOWOVoletHomeView, Stop)

IOWOVoletHomeView::IOWOVoletHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type flags):
        GenlistItemBase(_evas, _parent, string("WOVolet_") + style_addition, flags),
        IOBaseElement(_io)
{
}

IOWOVoletHomeView::~IOWOVoletHomeView()
{
}

void IOWOVoletHomeView::ioDeleted()
{
        IOBaseElement::ioDeleted();

        DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWOVoletHomeView::getPartItem(Evas_Object *obj, string part)
{
        Evas_Object *o = NULL;

        if (!io) return o;

        if (part == "calaos.button.up")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/up_arrow");
                elm_object_style_set(o, "calaos/action_button/yellow");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Up, this);
        }
        else if (part == "calaos.button.down")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/down_arrow");
                elm_object_style_set(o, "calaos/action_button/default");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Down, this);
        }
        else if (part == "calaos.button.stop")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/stop");
                elm_object_style_set(o, "calaos/action_button/default");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Stop, this);
        }

        initView();

        return o;
}

string IOWOVoletHomeView::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (!io) return text;

        if (part == "text")
                text = io->params["name"];

        return text;
}

void IOWOVoletHomeView::initView()
{
        if (!io || !item)
                return;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

        if (io->params["state"] == "false")
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

void IOWOVoletHomeView::updateView()
{
        if (!io || !item)
                return;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

        if (io->params["state"] == "false")
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

void IOWOVoletHomeView::buttonClickUp()
{
        if (!io) return;

        io->sendAction("up");
}

void IOWOVoletHomeView::buttonClickDown()
{
        if (!io) return;

        io->sendAction("down");
}

void IOWOVoletHomeView::buttonClickStop()
{
        if (!io) return;

        io->sendAction("stop");
}
