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
#include "IOWODaliHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWODaliHomeView, On)
ITEM_BUTTON_CALLBACK(IOWODaliHomeView, Off)
ITEM_BUTTON_CALLBACK(IOWODaliHomeView, More)
ITEM_BUTTON_CALLBACK(IOWODaliHomeView, Less)

IOWODaliHomeView::IOWODaliHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
        GenlistItemBase(_evas, _parent, string("WODali_") + style_addition, _flags),
        IOBaseElement(_io)
{
}

IOWODaliHomeView::~IOWODaliHomeView()
{
}

void IOWODaliHomeView::ioDeleted()
{
        IOBaseElement::ioDeleted();

        DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWODaliHomeView::getPartItem(Evas_Object *obj, string part)
{
        Evas_Object *o = NULL;

        if (!io) return o;

        else if (part == "calaos.button.on")
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
        else if (part == "calaos.button.more")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_more");
                elm_object_style_set(o, "calaos/action_button/default");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_More, this);
        }
        else if (part == "calaos.button.less")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_less");
                elm_object_style_set(o, "calaos/action_button/default");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Less, this);
        }
        else if (part == "calaos.slider")
        {
                slider = new EdjeObject(ApplicationMain::getTheme(), evas);
                slider->setAutoDelete(true);
                slider->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliHomeView::sliderObjectDeleted));
                slider->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliHomeView::sliderSignalCallback));
                slider->LoadEdje("calaos/slider/horizontal/default");
                slider->Show();

                slider->setDragValue("slider", io->getDaliValueFromState() / 100.0, 0.0);

                o = slider->getEvasObject();
        }

        initView();

        return o;
}

string IOWODaliHomeView::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (!io) return text;

        if (part == "text")
        {
                text = io->params["name"];
        }
        else if (part == "text.value")
        {
                double value = io->getDaliValueFromState();
                int percent = (int)value;

                text = Utils::to_string(percent) + "%";
        }

        return text;
}

void IOWODaliHomeView::sliderObjectDeleted()
{
        slider = NULL;
}

void IOWODaliHomeView::initView()
{
        if (!io || !item)
                return;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_genlist_item_fields_update(item, "text.value", ELM_GENLIST_ITEM_FIELD_TEXT);

        if (io->getDaliValueFromState() > 0.0)
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

void IOWODaliHomeView::updateView()
{
        if (!io || !item)
                return;

        double value = io->getDaliValueFromState();

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_genlist_item_fields_update(item, "text.value", ELM_GENLIST_ITEM_FIELD_TEXT);
        slider->setDragValue("slider", value / 100.0, 0.0);

        if (value > 0.0)
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

void IOWODaliHomeView::sliderSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        if (emission == "slider,start")
        {
                elm_object_scroll_freeze_push(genlist);
        }
        else if (emission == "slider,move")
        {
        }
        else if (emission == "slider,changed")
        {
                double x;
                slider->getDragValue("slider", &x, NULL);

                string action = "set ";
                action += Utils::to_string((int)(x * 100.0));

                if (io) io->sendAction(action);

                elm_object_scroll_freeze_pop(genlist);
        }

}

void IOWODaliHomeView::buttonClickOn()
{
        if (!io) return;

        io->sendAction("true");
}

void IOWODaliHomeView::buttonClickOff()
{
        if (!io) return;

        io->sendAction("false");
}

void IOWODaliHomeView::buttonClickMore()
{
        if (!io) return;

        string action = "set ";
        action += Utils::to_string((int)(io->getDaliValueFromState() + 1));

        io->sendAction(action);
}

void IOWODaliHomeView::buttonClickLess()
{
        if (!io) return;

        string action = "set ";
        action += Utils::to_string((int)(io->getDaliValueFromState() - 1));

        io->sendAction(action);
}
