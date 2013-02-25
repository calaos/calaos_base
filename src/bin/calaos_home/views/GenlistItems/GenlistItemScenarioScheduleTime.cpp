/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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

#include "GenlistItemScenarioScheduleTime.h"
#include <ApplicationMain.h>

#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>

ITEM_BUTTON_CALLBACK(GenlistItemScenarioScheduleTime, Edit)
ITEM_BUTTON_CALLBACK(GenlistItemScenarioScheduleTime, Delete)

GenlistItemScenarioScheduleTime::GenlistItemScenarioScheduleTime(Evas *_evas, Evas_Object *_parent, void *data):
        GenlistItemBase(_evas, _parent, "scenario/schedule/time", ELM_GENLIST_ITEM_NONE, data)
{
}

GenlistItemScenarioScheduleTime::~GenlistItemScenarioScheduleTime()
{
}

string GenlistItemScenarioScheduleTime::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (part == "text")
        {
                text = "Exécution à 12h30";
        }

        itemEmitSignal("monday,active", "calaos");
        itemEmitSignal("tuesday,active", "calaos");
        itemEmitSignal("thirsday,active", "calaos");
        itemEmitSignal("friday,active", "calaos");

        return text;
}

Evas_Object *GenlistItemScenarioScheduleTime::getPartItem(Evas_Object *obj, string part)
{
        Evas_Object *o = NULL;

        if (part == "calaos.button.edit")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/edit");
                elm_object_style_set(o, "calaos/action_button/blue");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Edit, this);
        }
        else if (part == "calaos.button.delete")
        {
                o = elm_button_add(parent);
                Evas_Object *icon = elm_icon_add(o);
                elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/delete");
                elm_object_style_set(o, "calaos/action_button/blue");
                elm_object_content_set(o, icon);
                evas_object_smart_callback_add(o, "clicked", _item_button_Delete, this);
        }
        else if (part == "icon")
        {
                o = elm_icon_add(parent);
                elm_image_file_set(o, ApplicationMain::getTheme(), "calaos/icons/element/simple/play");
        }

        return o;
}

void GenlistItemScenarioScheduleTime::buttonClickEdit()
{

}

void GenlistItemScenarioScheduleTime::buttonClickDelete()
{

}
