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
#include "ActivityConfigView.h"

ActivityConfigView::ActivityConfigView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/config")
{
    setPartText("header.label", _("Control Panel"));

    contentView = new MainContentView(evas, parent);
    Swallow(contentView->getSmartObject(), "calaos.main.content");

    Evas_Object *btn = edje_object_part_external_object_get(edje, "button.back");
    elm_object_text_set(btn, _("Back to menu"));

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityConfigView::buttonPressed));
}

ActivityConfigView::~ActivityConfigView()
{
    DELETE_NULL(contentView)
}

void ActivityConfigView::resetView()
{
}

void ActivityConfigView::buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    button_clicked.emit(source);
}

