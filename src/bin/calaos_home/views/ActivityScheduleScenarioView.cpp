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
#include "ActivityScheduleScenarioView.h"
#include "ApplicationMain.h"
#include "GenlistItemScenarioHeader.h"
#include "GenlistItemScenarioScheduleTime.h"
#include "GenlistItemSimpleHeader.h"
#include "ActivityIntl.h"

ActivityScheduleScenarioView::ActivityScheduleScenarioView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/schedule_scenario"),
    schedule_list(NULL),
    month_list(NULL)
{
    setPartText("header.label", _("Scheduling"));

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonPressed));

    schedule_list = elm_genlist_add(parent);
    Swallow(schedule_list, "schedule.list");
    elm_object_style_set(schedule_list, "calaos");
    elm_genlist_homogeneous_set(schedule_list, true);
    evas_object_show(schedule_list);

    month_list = elm_genlist_add(parent);
    Swallow(month_list, "month.list");
    elm_object_style_set(month_list, "calaos");
    elm_genlist_homogeneous_set(month_list, true);
    evas_object_show(month_list);
    elm_genlist_multi_select_set(month_list, true);

    GenlistItemScenarioHeader *header;
    GenlistItemSimple *item;
    header = new GenlistItemScenarioHeader(evas, parent, _("Month of year"));
    header->Append(month_list);

    item = new GenlistItemSimple(evas, parent, _("All year"), true, false, NULL, "check");
    item->Append(month_list);
    item_all = item;
    item_all->setSelected(true);

    item = new GenlistItemSimple(evas, parent, _("January"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("February"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("March"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("April"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("May"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("June"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("July"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("August"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("September"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("October"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("November"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("December"), true, false, NULL, "check");
    item->Append(month_list);
    items_months.push_back(item);

<<<<<<< HEAD
    header = new GenlistItemScenarioHeader(evas, parent, "Périodes prédéfinies");
    header->Append(month_list);

    item = new GenlistItemSimple(evas, parent, _("Spring"), true, false, NULL, "check");
    item->Append(month_list);
    items_periods.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("Summer"), true, false, NULL, "check");
    item->Append(month_list);
    items_periods.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("Fall"), true, false, NULL, "check");
    item->Append(month_list);
    items_periods.push_back(item);

    item = new GenlistItemSimple(evas, parent, _("Winter"), true, false, NULL, "check");
    item->Append(month_list);
    items_periods.push_back(item);
=======
    /*
         * Can't use that for now. Seasons are not the same all over the world at the same monthes
         * (northern hemisphere/southern hemisphere have the opposite) We need to handle that
         * maybe from longitude/latitude in local_config.xml
         *
        header = new GenlistItemScenarioHeader(evas, parent, "Périodes prédéfinies");
        header->Append(month_list);

        item = new GenlistItemSimple(evas, parent, _("Spring"), true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, _("Summer"), true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, _("Fall"), true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, _("Winter"), true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);
        */
>>>>>>> Some work on Time scheduling

    //Set up selection callback
    item_all->item_selected.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::itemAllYearSelected));
    for (uint i = 0;i < items_months.size();i++)
        items_months[i]->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityScheduleScenarioView::itemMonthSelected), items_months[i]));
    for (uint i = 0;i < items_periods.size();i++)
        items_periods[i]->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityScheduleScenarioView::itemPeriodSelected), items_periods[i]));
}

ActivityScheduleScenarioView::~ActivityScheduleScenarioView()
{
    DELETE_NULL_FUNC(evas_object_del, schedule_list);
    DELETE_NULL_FUNC(evas_object_del, month_list);
}

void ActivityScheduleScenarioView::resetView()
{
}

void ActivityScheduleScenarioView::buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (source == "button.valid")
    {
        buttonValidPressed.emit(range_infos);
    }
    else if (source == "button.add")
    {
        //clear time range, we want a new one
        edit_range = TimeRange();
        editState = EDIT_START_TYPE;
        showTimeRangePopup();
    }
}

void ActivityScheduleScenarioView::showTimeRangePopup()
{
    pager_popup = elm_naviframe_add(parent);
    evas_object_show(pager_popup);

    spin_start_hours = spin_start_min = spin_start_sec = NULL;
    spin_end_hours = spin_end_min = spin_end_sec = NULL;

    createTimeSelectTypeList(NULL, NULL, "", "");

    //create popup
    popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, pager_popup);
    elm_object_style_set(popup, "calaos");
    elm_ctxpopup_direction_priority_set(popup,
                                        ELM_CTXPOPUP_DIRECTION_DOWN,
                                        ELM_CTXPOPUP_DIRECTION_RIGHT,
                                        ELM_CTXPOPUP_DIRECTION_LEFT,
                                        ELM_CTXPOPUP_DIRECTION_UP);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);
}

void ActivityScheduleScenarioView::createTimeSelectTypeList(void *data, Evas_Object *edje_object, string emission, string source)
{
    if ((editState == EDIT_START_TIME || editState == EDIT_START_TIME_OFFSET) && cycle)
    {
        editStatesHist.push(editState);
        editState = EDIT_END_TYPE;
    }

    if (editState == EDIT_END_TYPE)
    {
        if (edit_range.start_type == TimeRange::HTYPE_NORMAL ||
            edit_range.start_offset != 0)
        {
            edit_range.shour = Utils::to_string(elm_spinner_value_get(spin_start_hours));
            edit_range.smin = Utils::to_string(elm_spinner_value_get(spin_start_min));
            edit_range.ssec = Utils::to_string(elm_spinner_value_get(spin_start_sec));
        }
        else
        {
            edit_range.shour = "0";
            edit_range.smin = "0";
            edit_range.ssec = "0";
        }
    }

    Evas_Object *table = createPaddingTable(evas, parent, 320, 300);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label;
    GenlistItemSimpleHeader *header = NULL;
    if (editState == EDIT_START_TYPE)
    {
        title_label = _("Start of schedulling<br><small><light_blue>Start time of the scenario</light_blue></small>");
        header = new GenlistItemSimpleHeader(evas, glist, title_label);
        header->Append(glist);
    }
    else
    {
        title_label = _("End of schedulling<br><small><light_blue>End time of the scenario</light_blue></small>");
        header = new GenlistItemSimpleHeader(evas, glist, title_label, "navigation_back");
        header->Append(glist);
        header->setButtonLabel("button.back", "Début");
        header->button_click.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonHeaderBackClick));
    }
<<<<<<< HEAD

    GenlistItemSimple *item;

=======

    GenlistItemSimple *item;

>>>>>>> Some work on Time scheduling
    for (int i = 0;i < 4;i++)
    {
        switch (i)
        {
        case 0: item = new GenlistItemSimple(evas, glist, _("Use a fixed time"), true, false, new int(TimeRange::HTYPE_NORMAL)); break;
        case 1: item = new GenlistItemSimple(evas, glist, _("Use the sunrise"), true, false, new int(TimeRange::HTYPE_SUNRISE)); break;
        case 2: item = new GenlistItemSimple(evas, glist, _("Use the sunset"), true, false, new int(TimeRange::HTYPE_SUNSET)); break;
        case 3: item = new GenlistItemSimple(evas, glist, _("Use the midday sun"), true, false, new int(TimeRange::HTYPE_NOON)); break;
        default: break;
        }

        item->Append(glist, header);
        item->setAutoDeleteUserData(new DeletorT<int *>);
        item->item_selected.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::selectTimeType));
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, table, "calaos");
}

void ActivityScheduleScenarioView::selectTimeType(void *data)
{
    int *user_data = reinterpret_cast<int *>(data);

    if (editState == EDIT_START_TYPE)
    {
        editStatesHist.push(editState);
        edit_range.start_type = *user_data;
        if (edit_range.start_type == TimeRange::HTYPE_NORMAL)
            editState = EDIT_START_TIME;
        else
            editState = EDIT_START_OFFSET;
    }
    else if (editState == EDIT_END_TYPE)
    {
        edit_range.end_type = *user_data;
        editStatesHist.push(editState);
        if (edit_range.end_type == TimeRange::HTYPE_NORMAL)
            editState = EDIT_END_TIME;
        else
            editState = EDIT_END_OFFSET;
    }

    if (editState == EDIT_END_TIME ||
        editState == EDIT_START_TIME)
    {
        if (editState == EDIT_END_TIME)
            edit_range.end_type = TimeRange::HTYPE_NORMAL;
        else
            edit_range.start_type = TimeRange::HTYPE_NORMAL;
        showTimeSelection(NULL);
    }
    else
    {
        Evas_Object *table = createPaddingTable(evas, parent, 300, 300);

        Evas_Object *glist = elm_genlist_add(table);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(glist);

        string title_label = _("<b>Offset time</b><br><light_blue><small>You can choose to shift the sun time</small></light_blue>");
        GenlistItemSimpleHeader *header = new GenlistItemSimpleHeader(evas, glist, title_label, "navigation_back");
        header->Append(glist);
        header->setButtonLabel("button.back", "Début");
        header->button_click.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonHeaderBackClick));

        GenlistItemSimple *item = NULL;

        for (int i = 0;i < 3;i++)
        {
            switch (i)
            {
            case 0: item = new GenlistItemSimple(evas, glist, _("Use the exact sun time"), true, false, new int(i)); break;
            case 1: item = new GenlistItemSimple(evas, glist, _("Add time"), true, false, new int(i)); break;
            case 2: item = new GenlistItemSimple(evas, glist, _("Substract time"), true, false, new int(i)); break;
            default: break;
            }

            item->Append(glist, header);
            item->setAutoDeleteUserData(new DeletorT<int *>);
            item->item_selected.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::showTimeSelection));
        }

        elm_table_pack(table, glist, 1, 1, 1, 1);

        elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, table, "calaos");
    }
}

void ActivityScheduleScenarioView::showTimeSelection(void *data)
{
    int *user_data = reinterpret_cast<int *>(data);

    if (editState == EDIT_START_OFFSET && user_data)
    {
        editStatesHist.push(editState);
        if (*user_data == 0)
        {
            edit_range.start_offset = 0;
            edit_range.shour = "0";
            edit_range.smin = "0";
            edit_range.ssec = "0";

            if (cycle)
            {
                editState = EDIT_END_TYPE;
                createTimeSelectTypeList(NULL, NULL, "", "");

                return;
            }
            else
            {
                editState = EDIT_WEEK;
                showWeekSelection(NULL, NULL, "", "");

                return;
            }
        }
        else if (*user_data == 1)
        {
            edit_range.start_offset = 1;
            editState  = EDIT_START_TIME_OFFSET;
        }
        else if (*user_data == 2)
        {
            edit_range.start_offset = -1;
            editState  = EDIT_START_TIME_OFFSET;
        }
    }

    if (editState == EDIT_END_OFFSET && user_data)
    {
        editStatesHist.push(editState);
        if (*user_data == 0)
        {
            edit_range.end_offset = 0;
            edit_range.ehour = "0";
            edit_range.emin = "0";
            edit_range.esec = "0";

            editState = EDIT_WEEK;
            showWeekSelection(NULL, NULL, "", "");

            return;
        }
        else if (*user_data == 1)
        {
            edit_range.end_offset = 1;
            editState  = EDIT_END_TIME_OFFSET;
        }
        else if (*user_data == 2)
        {
            edit_range.end_offset = -1;
            editState  = EDIT_END_TIME_OFFSET;
        }
    }

    EdjeObject *page = new EdjeObject(ApplicationMain::getTheme(), evas);
    page->LoadEdje("calaos/popup/page/time_schedule");
    page->setAutoDelete(true);

    page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonBackClick));
<<<<<<< HEAD
    cout << "### editState (" << editState << ") < EDIT_END_TYPE (" << EDIT_END_TYPE << ")" << endl;
=======

>>>>>>> Some work on Time scheduling
    if (cycle && editState < EDIT_END_TYPE)
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::createTimeSelectTypeList));
    else
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::showWeekSelection));
    string t;
    if (editState == EDIT_START_TIME)
        t = _("<b>Choose a scheduling</b><br><light_blue><small>Start time of scenario</small></light_blue>");
    else if (editState == EDIT_END_TIME)
        t = _("<b>Choose a scheduling</b><br><light_blue><small>End time of scenario</small></light_blue>");
    else if (editState == EDIT_START_TIME_OFFSET)
        t = _("<b>Choose a time shift</b><br><light_blue><small>Shifting start of scenario</small></light_blue>");
    else if (editState == EDIT_END_TIME_OFFSET)
        t = _("<b>Choose a time shift</b><br><light_blue><small>Shifteng end of scenario</small></light_blue>");
    page->setPartText("text", t);

    int hour_value, min_value, sec_value;
    Evas_Object **spin_hour = NULL, **spin_min = NULL, **spin_sec = NULL;
    if (editState == EDIT_START_TIME || editState == EDIT_START_TIME_OFFSET)
    {
        from_string(edit_range.shour, hour_value);
        from_string(edit_range.smin, min_value);
        from_string(edit_range.ssec, sec_value);

        spin_hour = &spin_start_hours;
        spin_min = &spin_start_min;
        spin_sec = &spin_start_sec;
    }
    else if (editState == EDIT_END_TIME || editState == EDIT_END_TIME_OFFSET)
    {
        from_string(edit_range.ehour, hour_value);
        from_string(edit_range.emin, min_value);
        from_string(edit_range.esec, sec_value);

        spin_hour = &spin_end_hours;
        spin_min = &spin_end_min;
        spin_sec = &spin_end_sec;
    }

    *spin_hour = elm_spinner_add(parent);
    elm_object_style_set(*spin_hour, "calaos/time/vertical");
    elm_spinner_label_format_set(*spin_hour, _("%.0f<br><subtitle>Hours</subtitle>"));
    elm_spinner_min_max_set(*spin_hour, 0, 99);
    elm_spinner_step_set(*spin_hour, 1);
    elm_spinner_interval_set(*spin_hour, 0.15);
    elm_spinner_value_set(*spin_hour, hour_value);
    evas_object_show(*spin_hour);
    page->Swallow(*spin_hour, "spinner.hours", true);

    *spin_min = elm_spinner_add(parent);
    elm_object_style_set(*spin_min, "calaos/time/vertical");
    elm_spinner_label_format_set(*spin_min, _("%.0f<br><subtitle>Min.</subtitle>"));
    elm_spinner_min_max_set(*spin_min, 0, 59);
    elm_spinner_step_set(*spin_min, 1);
    elm_spinner_interval_set(*spin_min, 0.15);
    elm_spinner_value_set(*spin_min, min_value);
    evas_object_show(*spin_min);
    page->Swallow(*spin_min, "spinner.minutes", true);

    *spin_sec = elm_spinner_add(parent);
    elm_object_style_set(*spin_sec, "calaos/time/vertical");
    elm_spinner_label_format_set(*spin_sec, _("%.0f<br><subtitle>Sec.</subtitle>"));
    elm_spinner_min_max_set(*spin_sec, 0, 59);
    elm_spinner_step_set(*spin_sec, 1);
    elm_spinner_interval_set(*spin_sec, 0.15);
    elm_spinner_value_set(*spin_sec, sec_value);
    evas_object_show(*spin_sec);
    page->Swallow(*spin_sec, "spinner.seconds", true);

    evas_object_size_hint_min_set(page->getEvasObject(), 300, 300);
    page->Show();

    elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void ActivityScheduleScenarioView::showWeekSelection(void *data, Evas_Object *edje_object, string emission, string source)
{
    if (cycle)
    {
        if (edit_range.end_type == TimeRange::HTYPE_NORMAL ||
            edit_range.end_offset != 0)
        {
            edit_range.ehour = Utils::to_string(elm_spinner_value_get(spin_end_hours));
            edit_range.emin = Utils::to_string(elm_spinner_value_get(spin_end_min));
            edit_range.esec = Utils::to_string(elm_spinner_value_get(spin_end_sec));
        }
    }
    else
    {
        edit_range.ehour = edit_range.shour;
        edit_range.emin = edit_range.smin;
        edit_range.esec = edit_range.ssec;
    }

    Evas_Object *table = createPaddingTable(evas, parent, 300, 300);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_genlist_multi_select_set(glist, true);
    evas_object_show(glist);

    string title_label = _("Days of the week<br><small><light_blue>Days of the week when scenario is executed.</light_blue></small>");
    GenlistItemSimpleHeader *header = new GenlistItemSimpleHeader(evas, glist, title_label, "navigation");
    header->Append(glist);

    if (cycle)
<<<<<<< HEAD
        header->setButtonLabel("button.back", "Fin");
    else
        header->setButtonLabel("button.back", "Début");
=======
        header->setButtonLabel("button.back", _("End"));
    else
        header->setButtonLabel("button.back", _("Beginning"));
>>>>>>> Some work on Time scheduling
    header->button_click.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::headerWeekButtonClick));

    week_days.clear();
    week_days.reserve(8);
    GenlistItemSimple *item;

    for (int i = 0;i < 8;i++)
    {
        string label;
        switch (i)
        {
        case 0: label = _("Everyday"); break;
        case 1: label = _("Monday"); break;
        case 2: label = _("Tuesday"); break;
        case 3: label = _("Wednesday"); break;
        case 4: label = _("Thursday"); break;
        case 5: label = _("Friday"); break;
        case 6: label = _("Saturday"); break;
        case 7: label = _("Sunday"); break;
        default: label = _("ERROR");
        }

        item = new GenlistItemSimple(evas, glist, label, true, false, NULL, "check");
        item->Append(glist, header);
        week_days.push_back(item);

        if (i == 0)
        {
            item->item_selected.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::unselectWeekDays));
            item->setSelected(true);
        }
        else
        {
            item->item_selected.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::unselectAllWeekDays));
        }
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, table, "calaos");
}

void ActivityScheduleScenarioView::headerWeekButtonClick(string bt)
{
    if (bt == "button.back")
    {
        elm_naviframe_item_pop(pager_popup);
    }
    else if (bt == "button.valid")
    {
        if (week_days[0]->isSelected() || week_days[1]->isSelected())
            range_infos.range_monday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[2]->isSelected())
            range_infos.range_tuesday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[3]->isSelected())
            range_infos.range_wednesday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[4]->isSelected())
            range_infos.range_thursday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[5]->isSelected())
            range_infos.range_friday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[6]->isSelected())
            range_infos.range_saturday.push_back(edit_range);
        if (week_days[0]->isSelected() || week_days[7]->isSelected())
            range_infos.range_sunday.push_back(edit_range);

        cDebug() <<  "New TimeRange: " << edit_range.toString();

        elm_ctxpopup_dismiss(popup);
    }
}

void ActivityScheduleScenarioView::buttonBackClick(void *data, Evas_Object *edje_object, string emission, string source)
{
    editState = editStatesHist.top();
    editStatesHist.pop();
    elm_naviframe_item_pop(pager_popup);
}

void ActivityScheduleScenarioView::buttonHeaderBackClick(string button)
{
    editState = editStatesHist.top();
    editStatesHist.pop();
    elm_naviframe_item_pop(pager_popup);
}

void ActivityScheduleScenarioView::unselectWeekDays(void *data)
{
    for (uint i = 1;i < week_days.size();i++)
        week_days[i]->setSelected(false);
}

void ActivityScheduleScenarioView::unselectAllWeekDays(void *data)
{
    week_days[0]->setSelected(false);
}

void ActivityScheduleScenarioView::itemAllYearSelected(void *data)
{
    if (item_all->isSelected())
    {
        for (uint i = 0;i < items_months.size();i++)
            items_months[i]->setSelected(false);
        for (uint i = 0;i < items_periods.size();i++)
            items_periods[i]->setSelected(false);
    }
}

void ActivityScheduleScenarioView::itemMonthSelected(void *data, GenlistItemSimple *item)
{
    if (!elm_genlist_selected_items_get(month_list))
    {
        item_all->setSelected(true);
    }
    else
        item_all->setSelected(false);
}

void ActivityScheduleScenarioView::itemPeriodSelected(void *data, GenlistItemSimple *item)
{
    if (!elm_genlist_selected_items_get(month_list))
        item_all->setSelected(true);
    else
        item_all->setSelected(false);
}

void ActivityScheduleScenarioView::reloadTimeRanges()
{
    int scount = range_infos.range_monday.size() +
                 range_infos.range_tuesday.size() +
                 range_infos.range_wednesday.size() +
                 range_infos.range_thursday.size() +
                 range_infos.range_friday.size() +
                 range_infos.range_saturday.size() +
                 range_infos.range_sunday.size();

    while (scount > 0)
    {
        //Load shedule items
    }
/*
    //------------TEST schedule list
    for (int i = 0;i < 5;i++)
    {
        GenlistItemScenarioScheduleTime *item = new GenlistItemScenarioScheduleTime(evas, parent);
        item->Append(schedule_list);
    }
    //----------------
*/
}
