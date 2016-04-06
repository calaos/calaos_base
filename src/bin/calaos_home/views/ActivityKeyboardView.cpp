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
#include "ActivityKeyboardView.h"

ActivityKeyboardView::ActivityKeyboardView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/keyboard")
{
    setPartText("header.label", "Clavier");

    keyboard = new KeyboardView(evas, parent);
    Swallow(keyboard, "keyboard");

    textblock = elm_entry_add(parent);
    elm_entry_scrollable_set(textblock, true);
    Swallow(textblock, "textblock");
    evas_object_show(textblock);
    elm_object_focus_set(textblock, true);

    Evas_Object *btn = edje_object_part_external_object_get(edje, "button.valid");
    elm_object_text_set(btn, _("Valider"));

    btn = edje_object_part_external_object_get(edje, "button.cleartext");
    elm_object_text_set(btn, _("Tout effacer"));

    addCallback("button.cleartext", "pressed", sigc::mem_fun(*this, &ActivityKeyboardView::clearTextCb));
    addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityKeyboardView::validInputCb));
}

ActivityKeyboardView::~ActivityKeyboardView()
{
    DELETE_NULL(keyboard);
    DELETE_NULL_FUNC(evas_object_del, textblock);
}

void ActivityKeyboardView::resetView()
{
}

void ActivityKeyboardView::setMultiline(bool multiline)
{
    if (multiline)
        elm_object_style_set(textblock, "calaos/multiline");
    else
        elm_object_style_set(textblock, "calaos");
}

void ActivityKeyboardView::setSubtitle(std::string subtitle)
{
    setPartText("module.subtitle", subtitle);
}

void ActivityKeyboardView::clearTextCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    clearText.emit();
    elm_entry_entry_set(textblock, "");
}

void ActivityKeyboardView::validInputCb(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    std::string val = elm_entry_entry_get(textblock);
    validPressed.emit(val);
}

void ActivityKeyboardView::setText(std::string t)
{
    elm_entry_entry_set(textblock, t.c_str());
}
