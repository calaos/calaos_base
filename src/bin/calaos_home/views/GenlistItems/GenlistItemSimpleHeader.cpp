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

#include "ApplicationMain.h"
#include "GenlistItemSimpleHeader.h"

ITEM_BUTTON_CALLBACK(GenlistItemSimpleHeader, Back)
ITEM_BUTTON_CALLBACK(GenlistItemSimpleHeader, Valid)

GenlistItemSimpleHeader::GenlistItemSimpleHeader(Evas *_evas, Evas_Object *_parent, std::string _label, std::string style_addition):
    GenlistItemBase(_evas, _parent, std::string("simple_header") + std::string((style_addition != "")? "/" + style_addition:""), ELM_GENLIST_ITEM_GROUP),
    label(_label)
{
}

GenlistItemSimpleHeader::~GenlistItemSimpleHeader()
{
}

std::string GenlistItemSimpleHeader::getLabelItem(Evas_Object *obj, std::string part)
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

Evas_Object *GenlistItemSimpleHeader::getPartItem(Evas_Object *obj, std::string part)
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
        evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _button_mouse_up_cb, NULL);
    }
    else if (part == "button.back")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/action_button/label/back");
        elm_object_text_set(o, bt_labels[part].c_str());
        evas_object_smart_callback_add(o, "clicked", _item_button_Back, this);
        evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _button_mouse_up_cb, NULL);
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
