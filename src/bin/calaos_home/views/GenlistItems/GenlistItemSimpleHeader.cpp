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

#include "GenlistItemSimpleHeader.h"
#include "ApplicationMain.h"

ITEM_BUTTON_CALLBACK(GenlistItemSimpleHeader, Back)
ITEM_BUTTON_CALLBACK(GenlistItemSimpleHeader, Valid)

GenlistItemSimpleHeader::GenlistItemSimpleHeader(Evas *_evas, Evas_Object *_parent, string _label, string style_addition):
    GenlistItemBase(_evas, _parent, string("simple_header") + string((style_addition != "")? "/" + style_addition:""), ELM_GENLIST_ITEM_GROUP),
    label(_label)
{
}

GenlistItemSimpleHeader::~GenlistItemSimpleHeader()
{
}

string GenlistItemSimpleHeader::getLabelItem(Evas_Object *obj, string part)
{
    return label;
}

Evas_Object *GenlistItemSimpleHeader::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "button.valid")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/popup/valid");
        elm_object_style_set(o, "calaos/action_button/green");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Valid, this);
    }
    else if (part == "button.back")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/action_button/label/back");
        elm_object_text_set(o, bt_labels[part].c_str());
        evas_object_smart_callback_add(o, "clicked", _item_button_Back, this);
    }

    return o;
}

void GenlistItemSimpleHeader::buttonClickValid()
{
    button_click.emit("button.valid");
}

void GenlistItemSimpleHeader::buttonClickBack()
{
    button_click.emit("button.back");
}
