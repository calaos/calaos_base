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
#include "IOWOVoletSmartHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Up)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Down)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Stop)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, ImpulseUp)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, ImpulseDown)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Set25)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Set50)
ITEM_BUTTON_CALLBACK(IOWOVoletSmartHomeView, Set75)

IOWOVoletSmartHomeView::IOWOVoletSmartHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("WOVoletSmart_") + style_addition, _flags),
    IOBaseElement(_io),
    window_slider(NULL)
{
}

IOWOVoletSmartHomeView::~IOWOVoletSmartHomeView()
{
}

void IOWOVoletSmartHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWOVoletSmartHomeView::getPartItem(Evas_Object *obj, string part)
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
    else if (part == "calaos.button.impulse_up")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/impulse_up");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_ImpulseUp, this);
    }
    else if (part == "calaos.button.impulse_down")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/impulse_down");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_ImpulseDown, this);
    }
    else if (part == "calaos.button.25")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/action_button/label");
        elm_object_text_set(o, "25%");
        evas_object_smart_callback_add(o, "clicked", _item_button_Set25, this);
    }
    else if (part == "calaos.button.50")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/action_button/label");
        elm_object_text_set(o, "50%");
        evas_object_smart_callback_add(o, "clicked", _item_button_Set50, this);
    }
    else if (part == "calaos.button.75")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/action_button/label");
        elm_object_text_set(o, "75%");
        evas_object_smart_callback_add(o, "clicked", _item_button_Set75, this);
    }
    else if (part == "calaos.window.slider")
    {
        window_slider = new EdjeObject(ApplicationMain::getTheme(), evas);
        window_slider->setAutoDelete(true);
        window_slider->object_deleted.connect(sigc::mem_fun(*this, &IOWOVoletSmartHomeView::sliderObjectDeleted));
        window_slider->LoadEdje("calaos/shutter/slider");
        window_slider->Show();

        o = window_slider->getEvasObject();
    }

    initView();

    return o;
}

string IOWOVoletSmartHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
    {
        text = io->params["name"];
    }
    else if (part == "shutter.state")
    {
        int percent = io->getPercentVoletSmart();

        if (percent == 0)
            text = "Etat : <light_blue>Ouvert.</light_blue>";
        else if (percent > 0 && percent < 50)
            text = "Etat : <light_blue>Ouvert à " + Utils::to_string(percent) + "%.</light_blue>";
        else if (percent >= 50 && percent < 100)
            text = "Etat : <light_blue>Fermé à " + Utils::to_string(percent) + "%.</light_blue>";
        else if (percent == 100)
            text = "Etat : <light_blue>Fermé.</light_blue>";
    }
    else if (part == "shutter.action")
    {
        string status = io->getStatusVoletSmart();
        if (status == "stop" || status == "")
            text = "Action : <light_blue>Arrêté</light_blue>";
        else if (status == "down")
            text = "Action : <light_blue>Fermeture.</light_blue>";
        else if (status == "up")
            text = "Action : <light_blue>Ouverture.</light_blue>";
    }

    return text;
}

void IOWOVoletSmartHomeView::sliderObjectDeleted()
{
    window_slider = NULL;
}

void IOWOVoletSmartHomeView::initView()
{
    updateView();
}

void IOWOVoletSmartHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "shutter.state", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "shutter.action", ELM_GENLIST_ITEM_FIELD_TEXT);

    int percent = io->getPercentVoletSmart();

    if (percent >= 100)
    {
        itemEmitSignal("text,active,blue", "calaos");
        itemEmitSignal("on,normal", "calaos");
    }
    else
    {
        itemEmitSignal("text,inactive", "calaos");
        itemEmitSignal("off,normal", "calaos");
    }

    if (window_slider) window_slider->setDragValue("object.shutter", 0.0, (double)percent / 100.0);
}

void IOWOVoletSmartHomeView::buttonClickUp()
{
    if (!io) return;

    io->sendAction("up");
}

void IOWOVoletSmartHomeView::buttonClickDown()
{
    if (!io) return;

    io->sendAction("down");
}

void IOWOVoletSmartHomeView::buttonClickStop()
{
    if (!io) return;

    io->sendAction("stop");
}

void IOWOVoletSmartHomeView::buttonClickImpulseUp()
{
    if (!io) return;

    io->sendAction("up 1");
}

void IOWOVoletSmartHomeView::buttonClickImpulseDown()
{
    if (!io) return;

    io->sendAction("down 1");
}

void IOWOVoletSmartHomeView::buttonClickSet25()
{
    if (!io) return;

    io->sendAction("set 25");
}

void IOWOVoletSmartHomeView::buttonClickSet50()
{
    if (!io) return;

    io->sendAction("set 50");
}

void IOWOVoletSmartHomeView::buttonClickSet75()
{
    if (!io) return;

    io->sendAction("set 75");
}
