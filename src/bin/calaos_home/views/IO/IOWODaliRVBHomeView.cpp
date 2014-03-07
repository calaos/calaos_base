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
#include "IOWODaliRVBHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, On)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, Off)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, RedMore)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, RedLess)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, GreenMore)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, GreenLess)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, BlueMore)
ITEM_BUTTON_CALLBACK(IOWODaliRVBHomeView, BlueLess)

IOWODaliRVBHomeView::IOWODaliRVBHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("WODaliRVB_") + style_addition, _flags),
    IOBaseElement(_io)
{
}

IOWODaliRVBHomeView::~IOWODaliRVBHomeView()
{
}

void IOWODaliRVBHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOWODaliRVBHomeView::getPartItem(Evas_Object *obj, string part)
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
    else if (part == "calaos.button.red.more")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_more");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_RedMore, this);
    }
    else if (part == "calaos.button.red.less")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_less");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_RedLess, this);
    }
    else if (part == "calaos.button.green.more")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_more");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_GreenMore, this);
    }
    else if (part == "calaos.button.green.less")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_less");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_GreenLess, this);
    }
    else if (part == "calaos.button.blue.more")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_more");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_BlueMore, this);
    }
    else if (part == "calaos.button.blue.less")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/slider_less");
        elm_object_style_set(o, "calaos/action_button/default");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_BlueLess, this);
    }
    else if (part == "calaos.slider.red")
    {
        slider_red = new EdjeObject(ApplicationMain::getTheme(), evas);
        slider_red->setAutoDelete(true);
        slider_red->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderRedObjectDeleted));
        slider_red->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallback), slider_red);
        slider_red->LoadEdje("calaos/slider/horizontal/red");
        slider_red->Show();

        int r, g, b;
        io->getRGBValueFromState(r, g, b);
        slider_red->setDragValue("slider", r / 100.0, 0.0);

        o = slider_red->getEvasObject();
    }
    else if (part == "calaos.slider.green")
    {
        slider_green = new EdjeObject(ApplicationMain::getTheme(), evas);
        slider_green->setAutoDelete(true);
        slider_green->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderRedObjectDeleted));
        slider_green->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallback), slider_green);
        slider_green->LoadEdje("calaos/slider/horizontal/green");
        slider_green->Show();

        int r, g, b;
        io->getRGBValueFromState(r, g, b);
        slider_green->setDragValue("slider", g / 100.0, 0.0);

        o = slider_green->getEvasObject();
    }
    else if (part == "calaos.slider.blue")
    {
        slider_blue = new EdjeObject(ApplicationMain::getTheme(), evas);
        slider_blue->setAutoDelete(true);
        slider_blue->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderRedObjectDeleted));
        slider_blue->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallback), slider_blue);
        slider_blue->LoadEdje("calaos/slider/horizontal/blue");
        slider_blue->Show();

        int r, g, b;
        io->getRGBValueFromState(r, g, b);
        slider_blue->setDragValue("slider", b / 100.0, 0.0);

        o = slider_blue->getEvasObject();
    }
    else if (part == "color.preview")
    {
        int r, g, b, a = 255;
        io->getRGBValueFromState(r, g, b);
        if (r == 0 && g == 0 && b == 0) a = 100;

        color_preview = evas_object_rectangle_add(evas);
        evas_object_color_set(color_preview, r, g, b, a);
        evas_object_show(color_preview);

        o = color_preview;
    }

    initView();

    return o;
}

string IOWODaliRVBHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
    {
        text = io->params["name"];
    }
    else if (part == "text.value.red")
    {
        int r, g, b;
        io->getRGBValueFromState(r, g, b);

        text = Utils::to_string(r) + "%";
    }
    else if (part == "text.value.green")
    {
        int r, g, b;
        io->getRGBValueFromState(r, g, b);

        text = Utils::to_string(g) + "%";
    }
    else if (part == "text.value.blue")
    {
        int r, g, b;
        io->getRGBValueFromState(r, g, b);

        text = Utils::to_string(b) + "%";
    }

    return text;
}

void IOWODaliRVBHomeView::sliderRedObjectDeleted()
{
    slider_red = NULL;
}

void IOWODaliRVBHomeView::sliderGreenObjectDeleted()
{
    slider_green = NULL;
}

void IOWODaliRVBHomeView::sliderBlueObjectDeleted()
{
    slider_blue = NULL;
}

void IOWODaliRVBHomeView::initView()
{
    if (!io || !item)
        return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    if (r > 0 || g > 0 || b > 0)
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

void IOWODaliRVBHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "text.value.red", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "text.value.green", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "text.value.blue", ELM_GENLIST_ITEM_FIELD_TEXT);
    elm_genlist_item_fields_update(item, "color.preview", ELM_GENLIST_ITEM_FIELD_CONTENT);

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    slider_red->setDragValue("slider", r / 100.0, 0.0);
    slider_green->setDragValue("slider", g / 100.0, 0.0);
    slider_blue->setDragValue("slider", b / 100.0, 0.0);

    if (r > 0 || g > 0 || b > 0)
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

void IOWODaliRVBHomeView::sliderSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
    EdjeObject *slider = reinterpret_cast<EdjeObject *>(data);
    if (!slider) return;

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
        int r, g, b;
        slider->getDragValue("slider", &x, NULL);

        io->getRGBValueFromState(r, g, b);

        if (slider == slider_red) r = x * 100;
        if (slider == slider_green) g = x * 100;
        if (slider == slider_blue) b = x * 100;

        string action = "set ";
        action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

        if (io) io->sendAction(action);

        elm_object_scroll_freeze_pop(genlist);
    }

}

void IOWODaliRVBHomeView::buttonClickOn()
{
    if (!io) return;

    io->sendAction("true");
}

void IOWODaliRVBHomeView::buttonClickOff()
{
    if (!io) return;

    io->sendAction("false");
}

void IOWODaliRVBHomeView::buttonClickRedMore()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    r += 5;
    if (r > 100) r = 100;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickRedLess()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    r -= 5;
    if (r < 0) r = 0;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickGreenMore()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    g += 5;
    if (g > 100) g = 100;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickGreenLess()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    g -= 5;
    if (g < 0) g = 0;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickBlueMore()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    b += 5;
    if (b > 100) b = 100;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickBlueLess()
{
    if (!io) return;

    int r, g, b;
    io->getRGBValueFromState(r, g, b);

    b -= 5;
    if (b < 0) b = 0;

    string action = "set ";
    action += Utils::to_string(io->computeStateFromRGBValue(r, g, b));

    io->sendAction(action);
}
