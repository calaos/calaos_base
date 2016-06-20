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

#include "GenlistItemScenarioScheduleTime.h"
#include <ApplicationMain.h>

#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>

ITEM_BUTTON_CALLBACK(GenlistItemScenarioScheduleTime, Edit)
ITEM_BUTTON_CALLBACK(GenlistItemScenarioScheduleTime, Delete)

GenlistItemScenarioScheduleTime::GenlistItemScenarioScheduleTime(Evas *_evas, Evas_Object *_parent, TimeRange &r, void *data):
    GenlistItemBase(_evas, _parent, "scenario/schedule/time", ELM_GENLIST_ITEM_NONE, data),
    range(r)
{
}

GenlistItemScenarioScheduleTime::~GenlistItemScenarioScheduleTime()
{
}

std::string GenlistItemScenarioScheduleTime::getLabelItem(Evas_Object *obj, std::string part)
{
    std::string text;

    if (part == "text")
    {
        bool same = range.isSameStartEnd();
        std::string starttxt;
        if (same)
            starttxt = _("Execute at ");
        else
            starttxt = _("Start at ");

        auto offsetString = [=](bool isstart)
        {
            if (isstart)
            {
                int h, m, s;
                from_string(range.shour, h);
                from_string(range.smin, m);
                from_string(range.ssec, s);
                int v = h * 3600 + m * 60 + s;

                if (range.start_offset == 1)
                    return std::string(" +") + Utils::time2string_digit(v);
                else if (range.start_offset == -1)
                    return std::string(" -") + Utils::time2string_digit(v);

            }
            else
            {
                int h, m, s;
                from_string(range.ehour, h);
                from_string(range.emin, m);
                from_string(range.esec, s);
                int v = h * 3600 + m * 60 + s;

                if (range.end_offset == 1)
                    return std::string(" +") + Utils::time2string_digit(v);
                else
                    if (range.end_offset == -1)
                        return std::string(" -") + Utils::time2string_digit(v);
            }

            return std::string();
        };

        if (range.start_type == TimeRange::HTYPE_NORMAL)
            text = starttxt + Utils::time2string_digit(range.getStartTimeSec());
        else if (range.start_type == TimeRange::HTYPE_SUNRISE)
            text = starttxt + _("Sunrise") + offsetString(true) + " (" + Utils::time2string_digit(range.getStartTimeSec()) + ")";
        else if (range.start_type == TimeRange::HTYPE_SUNSET)
            text = starttxt + _("Sunset") + offsetString(true) + " (" + Utils::time2string_digit(range.getStartTimeSec()) + ")";
        else if (range.start_type == TimeRange::HTYPE_NOON)
            text = starttxt + _("Noon") + offsetString(true) + " (" + Utils::time2string_digit(range.getStartTimeSec()) + ")";

        if (!same)
        {
            text += ", ";

            starttxt = _("stop at ");

            if (range.end_type == TimeRange::HTYPE_NORMAL)
                text += starttxt + Utils::time2string_digit(range.getEndTimeSec());
            else if (range.end_type == TimeRange::HTYPE_SUNRISE)
                text += starttxt + _("Sunrise") + offsetString(false) + " (" + Utils::time2string_digit(range.getEndTimeSec()) + ")";
            else if (range.end_type == TimeRange::HTYPE_SUNSET)
                text += starttxt + _("Sunset") + offsetString(false) + " (" + Utils::time2string_digit(range.getEndTimeSec()) + ")";
            else if (range.end_type == TimeRange::HTYPE_NOON)
                text += starttxt + _("Noon") + offsetString(false) + " (" + Utils::time2string_digit(range.getEndTimeSec()) + ")";
        }
    }

    return text;
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

Evas_Object *GenlistItemScenarioScheduleTime::getPartItem(Evas_Object *obj, std::string part)
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
        evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _button_mouse_up_cb, NULL);
    }
    else if (part == "calaos.button.delete")
    {
        o = elm_button_add(parent);
        Evas_Object *icon = elm_icon_add(o);
        elm_image_file_set(icon, ApplicationMain::getTheme(), "calaos/icons/action_button/delete");
        elm_object_style_set(o, "calaos/action_button/blue");
        elm_object_content_set(o, icon);
        evas_object_smart_callback_add(o, "clicked", _item_button_Delete, this);
        evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_UP, _button_mouse_up_cb, NULL);
    }
    else if (part == "icon")
    {
        o = elm_icon_add(parent);
        elm_image_file_set(o, ApplicationMain::getTheme(), "calaos/icons/element/simple/play");
    }

    auto setDay = [=](std::string day, int active)
    {
        if (active == 1)
            itemEmitSignal(day + ",active", "calaos");
        else
            itemEmitSignal(day + ",inactive", "calaos");
    };

    setDay("monday", range.dayOfWeek[0]);
    setDay("tuesday", range.dayOfWeek[1]);
    setDay("wednesday", range.dayOfWeek[2]);
    setDay("thursday", range.dayOfWeek[3]);
    setDay("friday", range.dayOfWeek[4]);
    setDay("saturday", range.dayOfWeek[5]);
    setDay("sunday", range.dayOfWeek[6]);

    return o;
}

void GenlistItemScenarioScheduleTime::buttonClickEdit()
{
    edit_click.emit();
}

void GenlistItemScenarioScheduleTime::buttonClickDelete()
{
    del_click.emit();
}
