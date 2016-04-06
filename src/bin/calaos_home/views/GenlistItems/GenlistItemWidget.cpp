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

#include "GenlistItemWidget.h"
#include <ApplicationMain.h>

GenlistItemWidget::GenlistItemWidget(Evas *_evas, Evas_Object *_parent, ModuleDef mod, void *data):
    GenlistItemBase(_evas, _parent, "widget", ELM_GENLIST_ITEM_NONE, data),
    modinfo(mod)
{
}

GenlistItemWidget::~GenlistItemWidget()
{
}

std::string GenlistItemWidget::getLabelItem(Evas_Object *obj, std::string part)
{
    if (part == "title")
        return modinfo.mod_name;
    else if (part == "desc")
        return modinfo.mod_desc;

    return "";
}

Evas_Object *GenlistItemWidget::getPartItem(Evas_Object *obj, std::string part)
{
    Evas_Object *o = nullptr;

    if (part == "icon")
    {
        o = elm_icon_add(parent);
        elm_image_preload_disabled_set(o, false);
        if (!elm_image_file_set(o, modinfo.mod_icon.c_str(), "icon"))
        {
            elm_image_file_set(o, ApplicationMain::getTheme(), "calaos/widget/icon/default");
        }
    }

    return o;
}
