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
#include "IOWODaliRVBHomeView.h"
#include <ApplicationMain.h>
#include "ColorUtils.h"

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
        slider_red->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallbackRed), slider_red);
        slider_red->LoadEdje("calaos/slider/horizontal/red");
        slider_red->Show();

        ColorValue c(io->params["state"]);
        if (c.isValid())
            slider_red->setDragValue("slider", c.getRed() / 255.0, 0.0);

        o = slider_red->getEvasObject();
    }
    else if (part == "calaos.slider.green")
    {
        slider_green = new EdjeObject(ApplicationMain::getTheme(), evas);
        slider_green->setAutoDelete(true);
        slider_green->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderRedObjectDeleted));
        slider_green->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallbackGreen), slider_green);
        slider_green->LoadEdje("calaos/slider/horizontal/green");
        slider_green->Show();

        ColorValue c(io->params["state"]);
        if (c.isValid())
            slider_green->setDragValue("slider", c.getGreen() / 255.0, 0.0);

        o = slider_green->getEvasObject();
    }
    else if (part == "calaos.slider.blue")
    {
        slider_blue = new EdjeObject(ApplicationMain::getTheme(), evas);
        slider_blue->setAutoDelete(true);
        slider_blue->object_deleted.connect(sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderRedObjectDeleted));
        slider_blue->addCallback("object", "*", sigc::mem_fun(*this, &IOWODaliRVBHomeView::sliderSignalCallbackBlue), slider_blue);
        slider_blue->LoadEdje("calaos/slider/horizontal/blue");
        slider_blue->Show();

        ColorValue c(io->params["state"]);
        if (c.isValid())
            slider_blue->setDragValue("slider", c.getBlue() / 255.0, 0.0);

        o = slider_blue->getEvasObject();
    }
    else if (part == "color.preview")
    {
        int a = 255;

        ColorValue c(io->params["state"]);

        if (c.getRed() == 0 && c.getGreen() == 0 && c.getBlue() == 0)
            a = 100;

        color_preview = evas_object_rectangle_add(evas);
        evas_object_color_set(color_preview, c.getRed(), c.getGreen(), c.getBlue(), a);
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
        ColorValue c(io->params["state"]);

        text = Utils::to_string(int(c.getRed() * 100.0 / 255.0)) + "%";
    }
    else if (part == "text.value.green")
    {
        ColorValue c(io->params["state"]);

        text = Utils::to_string(int(c.getGreen() * 100.0 / 255.0)) + "%";
    }
    else if (part == "text.value.blue")
    {
        ColorValue c(io->params["state"]);

        text = Utils::to_string(int(c.getBlue() * 100.0 / 255.0)) + "%";
    }

    return text;
}

void IOWODaliRVBHomeView::sliderRedObjectDeleted()
{
    slider_red = nullptr;
}

void IOWODaliRVBHomeView::sliderGreenObjectDeleted()
{
    slider_green = nullptr;
}

void IOWODaliRVBHomeView::sliderBlueObjectDeleted()
{
    slider_blue = nullptr;
}

void IOWODaliRVBHomeView::initView()
{
    if (!io || !item)
        return;

    ColorValue c(io->params["state"]);

    if (c.getRed() > 0 || c.getGreen() > 0 || c.getBlue() > 0)
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

    ColorValue c(io->params["state"]);

    if (slider_red)
        slider_red->setDragValue("slider", c.getRed() / 255.0, 0.0);
    if (slider_green)
        slider_green->setDragValue("slider", c.getGreen() / 255.0, 0.0);
    if (slider_blue)
        slider_blue->setDragValue("slider", c.getBlue() / 255.0, 0.0);

    if (c.getRed() > 0 || c.getGreen() > 0 || c.getBlue() > 0)
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

void IOWODaliRVBHomeView::sliderSignalCallbackRed(void *data, Evas_Object *edje_object, string emission, string source)
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
        slider->getDragValue("slider", &x, NULL);

        ColorValue c(io->params["state"]);

        c.setRed(x * 100.0 * 255.0 / 100.0);

        string action = "set " + c.toString();

        if (io) io->sendAction(action);

        elm_object_scroll_freeze_pop(genlist);
    }

}

void IOWODaliRVBHomeView::sliderSignalCallbackGreen(void *data, Evas_Object *edje_object, string emission, string source)
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
        slider->getDragValue("slider", &x, NULL);

        ColorValue c(io->params["state"]);

        c.setGreen(x * 100.0 * 255.0 / 100.0);

        string action = "set " + c.toString();

        if (io) io->sendAction(action);

        elm_object_scroll_freeze_pop(genlist);
    }

}

void IOWODaliRVBHomeView::sliderSignalCallbackBlue(void *data, Evas_Object *edje_object, string emission, string source)
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
        slider->getDragValue("slider", &x, NULL);

        ColorValue c(io->params["state"]);

        c.setBlue(x * 100.0 * 255.0 / 100.0);

        string action = "set " + c.toString();

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

    ColorValue c(io->params["state"]);
    c.setRed(c.getRed() + 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickRedLess()
{
    if (!io) return;

    ColorValue c(io->params["state"]);
    c.setRed(c.getRed() - 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickGreenMore()
{
    if (!io) return;

    ColorValue c(io->params["state"]);
    c.setGreen(c.getGreen() + 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickGreenLess()
{
    if (!io) return;

    ColorValue c(io->params["state"]);
    c.setGreen(c.getGreen() - 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickBlueMore()
{
    if (!io) return;

    ColorValue c(io->params["state"]);
    c.setBlue(c.getBlue() + 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}

void IOWODaliRVBHomeView::buttonClickBlueLess()
{
    if (!io) return;

    ColorValue c(io->params["state"]);
    c.setBlue(c.getBlue() - 5);
    string action = "set " + c.toString();

    io->sendAction(action);
}
