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
#include "IOGenlistScenarioHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOGenlistScenarioHomeView, Go)

IOGenlistScenarioHomeView::IOGenlistScenarioHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
        GenlistItemBase(_evas, _parent, string("scenario_") + style_addition, _flags),
        IOBaseElement(_io)
{
}

IOGenlistScenarioHomeView::~IOGenlistScenarioHomeView()
{
}

void IOGenlistScenarioHomeView::ioDeleted()
{
        IOBaseElement::ioDeleted();

        DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOGenlistScenarioHomeView::getPartItem(Evas_Object *obj, string part)
{
        Evas_Object *o = NULL;

        if (!io) return o;

        if (part == "calaos.button.go")
        {
                object_button = o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);

                if (io->params["state"] == "true")
                        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/stop");
                else
                        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/play");

                elm_object_style_set(o, "calaos/action_button/default");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Go, this);
        }

        initView();

        return o;
}

string IOGenlistScenarioHomeView::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (!io) return text;

        if (part == "text")
                text = io->params["name"];

        return text;
}

void IOGenlistScenarioHomeView::initView()
{
        if (!io || !item)
                return;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

        //Don't change button state if it's not a SimpleScenario
        if (io->params["ioBoolState"] == "")
        {
                itemEmitSignal("text,inactive", "calaos");
                itemEmitSignal("off,anim", "calaos");

                elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/play");

                return;
        }

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

void IOGenlistScenarioHomeView::updateView()
{
        if (!io || !item)
                return;

        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);

        //Don't change button state if it's not a SimpleScenario
        if (io->params["ioBoolState"] == "")
        {
                itemEmitSignal("text,inactive", "calaos");
                itemEmitSignal("off,anim", "calaos");

                elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/play");

                return;
        }

        if (io->params["state"] != state)
        {

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

                if (io->params["state"] == "true")
                        elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/stop");
                else
                        elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/play");

                state = io->params["state"];
        }
}

void IOGenlistScenarioHomeView::buttonClickGo()
{
        if (!io) return;

        io->sendAction("true");

        if (io->params["ioBoolState"] == "")
        {
                itemEmitSignal("text,active,blue", "calaos");
                itemEmitSignal("on,anim", "calaos");

                elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/stop");

                //If it's not a SimpleScenario, just flash button when user click it.
                EcoreTimer::singleShot(0.7, sigc::mem_fun(*this, &IOGenlistScenarioHomeView::clickFlashButton_cb));
        }
}

void IOGenlistScenarioHomeView::clickFlashButton_cb()
{
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,anim", "calaos");

        elm_image_file_set(elm_object_content_get(object_button), ApplicationMain::getTheme(), "calaos/icons/action_button/play");
}
