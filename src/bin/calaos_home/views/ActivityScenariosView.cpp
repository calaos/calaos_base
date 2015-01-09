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
#include "ActivityScenariosView.h"
#include "GenlistItemScenarioSchedule.h"
#include "GenlistItemScenarioHeader.h"

ActivityScenariosView::ActivityScenariosView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/scenarios"),
    view_mode(VIEW_MODE_ALL)
{
    setPartText("header.label", _("Scenario management"));

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityScenariosView::buttonPressed));

    schedule_list = elm_genlist_add(parent);
    Swallow(schedule_list, "scenario_schedule.list");
    elm_object_style_set(schedule_list, "calaos");
    elm_genlist_homogeneous_set(schedule_list, true);
    evas_object_show(schedule_list);

    scenario_list = elm_genlist_add(parent);
    Swallow(scenario_list, "scenario.list");
    elm_object_style_set(scenario_list, "calaos");
    elm_genlist_homogeneous_set(scenario_list, true);
    evas_object_show(scenario_list);

    Evas_Object *btn = edje_object_part_external_object_get(edje, "button.calendar.today");
    elm_object_text_set(btn, _("Today"));

    btn = edje_object_part_external_object_get(edje, "button.create");
    elm_object_text_set(btn, _("Create a new scenario"));

    btn = edje_object_part_external_object_get(edje, "button.list.all");
    elm_object_text_set(btn, _("All"));

    btn = edje_object_part_external_object_get(edje, "button.list.light");
    elm_object_text_set(btn, _("Lights"));

    btn = edje_object_part_external_object_get(edje, "button.list.shutters");
    elm_object_text_set(btn, _("Shutters"));

    btn = edje_object_part_external_object_get(edje, "button.list.schedule");
    elm_object_text_set(btn, _("Scheduled"));

    //default to today
    time_t t = time(0);
    currDate = *localtime(&t);

    CalaosModel::Instance().getScenario()->scenario_change.connect([=](Scenario *) { reloadCalendar(); });
}

ActivityScenariosView::~ActivityScenariosView()
{
    DELETE_NULL_FUNC(evas_object_del, schedule_list);
    DELETE_NULL_FUNC(evas_object_del, scenario_list);
}

void ActivityScenariosView::resetView()
{
}

void ActivityScenariosView::ShowLoading()
{
    EmitSignal("show,loading", "calaos");
}

void ActivityScenariosView::HideLoading()
{
    EmitSignal("hide,loading", "calaos");
}

static void _calendar_cb(void *data, Evas_Object *obj, void *event_info)
{
    ActivityScenariosView *view = reinterpret_cast<ActivityScenariosView *>(data);
    if (view)
    {
        struct tm ndate;
        if (!elm_calendar_selected_time_get(obj, &ndate))
             return;

        view->setCalendarDate(ndate);
        view->reloadCalendar();
    }
}

void ActivityScenariosView::buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (source == "button.calendar")
    {
        Evas_Object *table = createPaddingTable(evas, parent, 330, 300, 10, 10);

        calendar = elm_calendar_add(parent);
        elm_object_style_set(calendar, "calaos");

        elm_calendar_first_day_of_week_set(calendar, ELM_DAY_MONDAY);

        const char *weekdays[] = { "DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM" };
        elm_calendar_weekdays_names_set(calendar, weekdays);

        evas_object_smart_callback_add(calendar, "changed", _calendar_cb, this);

        //Mark sundays
        struct tm sunday = { 0, 0, 12, 7, 0, 0, 6, 0, -1, 0, NULL };
        elm_calendar_mark_add(calendar, "checked", &sunday, ELM_CALENDAR_WEEKLY);
        elm_calendar_marks_draw(calendar);

        evas_object_size_hint_fill_set(calendar, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(calendar, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(calendar);

        elm_table_pack(table, calendar, 1, 1, 1, 1);

        Evas_Object *popup = elm_ctxpopup_add(parent);
        elm_object_content_set(popup, table);
        elm_object_style_set(popup, "calaos");
        elm_ctxpopup_direction_priority_set(popup,
                                            ELM_CTXPOPUP_DIRECTION_DOWN,
                                            ELM_CTXPOPUP_DIRECTION_LEFT,
                                            ELM_CTXPOPUP_DIRECTION_RIGHT,
                                            ELM_CTXPOPUP_DIRECTION_UP);

        Evas_Coord x,y;
        evas_pointer_canvas_xy_get(evas, &x, &y);
        evas_object_move(popup, x, y);
        evas_object_show(popup);
    }
    else if (source == "button.calendar.today")
    {
        tzset(); //Force reload of timezone data
        time_t t = time(0);
        currDate = *localtime(&t);
        reloadCalendar();
    }
    else if (source == "button.create")
    {
        buttonCreatePressed.emit();
    }
    else if (source == "button.list.all")
    {
        view_mode = VIEW_MODE_ALL;
        loadScenarioList();
    }
    else if (source == "button.list.light")
    {
        view_mode = VIEW_MODE_LIGHT;
        loadScenarioList();
    }
    else if (source == "button.list.shutters")
    {
        view_mode = VIEW_MODE_SHUTTER;
        loadScenarioList();
    }
    else if (source == "button.list.schedule")
    {
        view_mode = VIEW_MODE_SCHEDULE;
        loadScenarioList();
    }
}

void ActivityScenariosView::loadScenarioList()
{
    elm_genlist_clear(scenario_list);
    GenlistItemScenarioHeader *headerLight = NULL, *headerShutter = NULL, *headerOther = NULL;

    if (view_mode == VIEW_MODE_ALL ||
        view_mode == VIEW_MODE_SCHEDULE)
    {
        headerLight = new GenlistItemScenarioHeader(evas, parent, _("Lights scenarios"));
        headerLight->Append(scenario_list);
        headerShutter = new GenlistItemScenarioHeader(evas, parent, _("Shutters scenarios"));
        headerShutter->Append(scenario_list);
        headerOther = new GenlistItemScenarioHeader(evas, parent, _("Other Scenarios"));
        headerOther->Append(scenario_list);
    }
    else if (view_mode == VIEW_MODE_LIGHT)
    {
        headerLight = new GenlistItemScenarioHeader(evas, parent, _("Lights scenarios"));
        headerLight->Append(scenario_list);
    }
    else if (view_mode == VIEW_MODE_SHUTTER)
    {
        headerShutter = new GenlistItemScenarioHeader(evas, parent, _("Shutters scenarios"));
        headerShutter->Append(scenario_list);
    }

    //populate the scenario list
    list<Scenario *>::iterator it = CalaosModel::Instance().getScenario()->scenarios.begin();
    for (;it != CalaosModel::Instance().getScenario()->scenarios.end();it++)
    {
        Scenario *sc = *it;
        bool cont = true;

        if (sc->getFirstCategory() == "light" &&
            (view_mode == VIEW_MODE_ALL || view_mode == VIEW_MODE_LIGHT || view_mode == VIEW_MODE_SCHEDULE))
        {
            if (view_mode == VIEW_MODE_SCHEDULE)
            {
                if (sc->isScheduled())
                    cont = false;
            }
            else
                cont = false;
        }

        if (sc->getFirstCategory() == "shutter" &&
            (view_mode == VIEW_MODE_ALL || view_mode == VIEW_MODE_SHUTTER || view_mode == VIEW_MODE_SCHEDULE))
        {
            if (view_mode == VIEW_MODE_SCHEDULE)
            {
                if (sc->isScheduled())
                    cont = false;
            }
            else
                cont = false;
        }

        if (sc->getFirstCategory() == "other" &&
            (view_mode == VIEW_MODE_ALL || view_mode == VIEW_MODE_SCHEDULE))
        {
            if (view_mode == VIEW_MODE_SCHEDULE)
            {
                if (sc->isScheduled())
                    cont = false;
            }
            else
                cont = false;
        }

        if (cont)
            continue;

        GenlistItemScenarioSchedule *item = new GenlistItemScenarioSchedule(evas, parent, false, sc);
        item->schedule_add_click.connect(sigc::mem_fun(schedule_add_click, &sigc::signal<void, Scenario *>::emit));
        item->schedule_modify_click.connect(sigc::mem_fun(schedule_modify_click, &sigc::signal<void, Scenario *>::emit));
        item->schedule_del_click.connect(sigc::mem_fun(schedule_del_click, &sigc::signal<void, Scenario *>::emit));
        if (sc->getFirstCategory() == "light")
            item->InsertAfter(scenario_list, headerLight);
        else if (sc->getFirstCategory() == "shutter")
            item->InsertAfter(scenario_list, headerShutter);
        else
            item->InsertAfter(scenario_list, headerOther);
    }
}

void ActivityScenariosView::reloadCalendar()
{
    elm_genlist_clear(schedule_list);

    string weekday;
    switch (currDate.tm_wday)
    {
    case 0: weekday = _("Sunday"); break;
    case 1: weekday = _("Monday"); break;
    case 2: weekday = _("Tuesday"); break;
    case 3: weekday = _("Wednesday"); break;
    case 4: weekday = _("Thursday"); break;
    case 5: weekday = _("Friday"); break;
    case 6: weekday = _("Saturday"); break;
    default: break;
    }

    string month;
    switch (currDate.tm_mon)
    {
    case 0: month = _("January"); break;
    case 1: month = _("February"); break;
    case 2: month = _("Mars"); break;
    case 3: month = _("April"); break;
    case 4: month = _("May"); break;
    case 5: month = _("June"); break;
    case 6: month = _("July"); break;
    case 7: month = _("August"); break;
    case 8: month = _("September"); break;
    case 9: month = _("October"); break;
    case 10: month = _("November"); break;
    case 11: month = _("December"); break;
    default: break;
    }

    string label = _("On <blue>%1, %2 %3, %4</blue>");
    Utils::replace_str(label, "%1", weekday);
    Utils::replace_str(label, "%2", month);
    Utils::replace_str(label, "%3", Utils::to_string(currDate.tm_mday));
    Utils::replace_str(label, "%4", Utils::to_string(currDate.tm_year + 1900));
    setPartText("schedule.date", label);

    list<ScenarioSchedule> lst = CalaosModel::Instance().getScenario()->getScenarioForDate(currDate);
    for (auto i = lst.begin();i != lst.end();i++)
    {
        ScenarioSchedule sc = *i;
        GenlistItemScenarioSchedule *item = new GenlistItemScenarioSchedule(evas, parent, true, sc.scenario);
        item->setScheduleRange(sc.day, sc.timeRangeNum, currDate);
        item->schedule_add_click.connect(sigc::mem_fun(schedule_add_click, &sigc::signal<void, Scenario *>::emit));
        item->schedule_modify_click.connect(sigc::mem_fun(schedule_modify_click, &sigc::signal<void, Scenario *>::emit));
        item->schedule_del_click.connect(sigc::mem_fun(schedule_del_click, &sigc::signal<void, Scenario *>::emit));
        item->Append(schedule_list);
    }
}

void ActivityScenariosView::loadScenarios()
{
    reloadCalendar();
    loadScenarioList();
}
