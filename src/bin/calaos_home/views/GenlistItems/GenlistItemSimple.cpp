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
                        Evas_Object *icon = elm_icon_add(o);
                        elm_image_file_set(icon, ApplicationMain::getTheme(), button_icon.c_str());
                        elm_object_style_set(o, "calaos/action_button/blue");
                        elm_object_content_set(o, icon);
                        evas_object_smart_callback_add(o, "clicked", _item_button_Pressed, this);

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
