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
#include "IOInternalStringHomeView.h"
#include <ApplicationMain.h>

ITEM_BUTTON_CALLBACK(IOInternalStringHomeView, Text)

IOInternalStringHomeView::IOInternalStringHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io, string style_addition, Elm_Genlist_Item_Type _flags):
    GenlistItemBase(_evas, _parent, string("InternalString_") + style_addition + ((_io->params["rw"] == "true")?"/rw":""), _flags),
    IOBaseElement(_io)
{
}

IOInternalStringHomeView::~IOInternalStringHomeView()
{
}

void IOInternalStringHomeView::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item)
}

Evas_Object *IOInternalStringHomeView::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (!io) return o;

    if (part == "calaos.button.text")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/keyboard");
        elm_object_style_set(o, "calaos/action_button/pink");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Text, this);
    }

    initView();

    return o;
}

string IOInternalStringHomeView::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!io) return text;

    if (part == "text")
    {
        if (io->params["state"] == "")
            text = io->params["name"];
        else
            text = io->params["state"];
    }

    return text;
}

void IOInternalStringHomeView::initView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOInternalStringHomeView::updateView()
{
    if (!io || !item)
        return;

    elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
}

void IOInternalStringHomeView::buttonClickText()
{
    if (!io) return;

    ApplicationMain::Instance().ShowKeyboard(_("Change value"),
                                             sigc::mem_fun(*this, &IOInternalStringHomeView::changeTextCb),
                                             false);
}

void IOInternalStringHomeView::changeTextCb(string text)
{
    if (!io) return;

    io->sendAction(text);
}
