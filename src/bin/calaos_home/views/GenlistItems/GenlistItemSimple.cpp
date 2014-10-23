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

#include "GenlistItemSimple.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(GenlistItemSimple, Pressed)

GenlistItemSimple::GenlistItemSimple(Evas *_evas, Evas_Object *_parent, string _label, bool can_select, bool _multiline, void *data, string style_addition):
    GenlistItemBase(_evas, _parent,
                    string(can_select? "simple_select":"simple") + string(_multiline? "/multiline":"") + string((style_addition != "")? "/" + style_addition:""),
                    ELM_GENLIST_ITEM_NONE, data),
    label(_label),
    multiline(_multiline)
{
    if (multiline)
    {
        replace_str(label, "\n", "<br>");
        replace_str(label, "&", "&amp;");
    }
}

GenlistItemSimple::~GenlistItemSimple()
{
}

string GenlistItemSimple::getLabelItem(Evas_Object *obj, string part)
{
    return label;
}

static void
_button_mouse_up_cb(void *data,
                    Evas *evas EINA_UNUSED,
                    Evas_Object *obj,
                    void *event_info)
{
  Evas_Event_Mouse_Up *ev = (Evas_Event_Mouse_Up*)event_info;
  /*
   * Edje/Elementary HACK block event propagation of adding by adding flag
   * ON_HOLD to evas event This event is read bu elementary genlist code to see
   * if the item selection event have to be emited or not
   */
  ev->event_flags = (Evas_Event_Flags)(ev->event_flags | EVAS_EVENT_FLAG_ON_HOLD);
}

Evas_Object *GenlistItemSimple::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "icon")
    {
        if (icon != "")
        {
            o = elm_icon_add(parent);
            elm_image_file_set(o, ApplicationMain::getTheme(), icon.c_str());

            itemEmitSignal("icon,enabled", "calaos");
        }
        else
        {
            itemEmitSignal("icon,disabled", "calaos");
        }
    }
    else if (part == "button")
    {
        if (button_icon != "")
        {
            o = elm_button_add(parent);
            Evas_Object *_icon = elm_icon_add(o);
            elm_image_file_set(_icon, ApplicationMain::getTheme(), button_icon.c_str());
            elm_object_style_set(o, "calaos/action_button/blue");
            elm_object_content_set(o, _icon);
            evas_object_smart_callback_add(o, "clicked", _item_button_Pressed, this);
            evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _button_mouse_up_cb, NULL);

            itemEmitSignal("button,enabled", "calaos");
        }
        else
        {
            itemEmitSignal("button,disabled", "calaos");
        }
    }

    return o;
}

void GenlistItemSimple::buttonClickPressed()
{
    button_pressed.emit();
}

GenlistItemSimpleKeyValue::GenlistItemSimpleKeyValue(Evas *_evas, Evas_Object *_parent, string _label_key, string _label_value, void *data):
    GenlistItemBase(_evas, _parent, "simple_info", ELM_GENLIST_ITEM_NONE, data),
    label_key(_label_key),
    label_value(_label_value)
{
}

GenlistItemSimpleKeyValue::~GenlistItemSimpleKeyValue()
{
}

string GenlistItemSimpleKeyValue::getLabelItem(Evas_Object *obj, string part)
{
    if (part == "key.text")
        return label_key;
    if (part == "value.text")
        return label_value;

    return "?";
}

Evas_Object *GenlistItemSimpleKeyValue::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "icon")
    {
        if (icon != "")
        {
            o = elm_icon_add(parent);
            elm_image_file_set(o, ApplicationMain::getTheme(), icon.c_str());

            itemEmitSignal("icon,enabled", "calaos");
        }
        else
        {
            itemEmitSignal("icon,disabled", "calaos");
        }
    }

    return o;
}
