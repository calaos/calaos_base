/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef ActivityScheduleScenarioView_H
#define ActivityScheduleScenarioView_H

#include <Utils.h>

#include "ActivityView.h"
#include "CalaosModel.h"
#include "GenlistItemSimple.h"


class ActivityScheduleScenarioView: public ActivityView
{
private:
    TimeRangeInfos range_infos;
    bool is_edit = false; //true if range is edited from an existing one

    Evas_Object *schedule_list, *month_list;

    GenlistItemSimple *item_all;
    std::vector<GenlistItemSimple *> items_months;
    std::vector<GenlistItemSimple *> items_periods;

    Evas_Object *popup;
    Evas_Object *pager_popup;

    Evas_Object *spin_start_hours;
    Evas_Object *spin_start_min;
    Evas_Object *spin_start_sec;

    Evas_Object *spin_end_hours;
    Evas_Object *spin_end_min;
    Evas_Object *spin_end_sec;

    std::vector<GenlistItemSimple *> week_days;

    //store current data we are editing, also used to set default value
    TimeRange edit_range;
    TimeRange old_range;

    enum { EDIT_START_TYPE = 0, EDIT_START_TIME, EDIT_START_OFFSET, EDIT_START_TIME_OFFSET,
           EDIT_END_TYPE, EDIT_END_TIME, EDIT_END_OFFSET, EDIT_END_TIME_OFFSET,
           EDIT_WEEK};
    int editState;

    std::stack<int> editStatesHist;

    bool cycle;

    void showTimeRangePopup();

    void buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void reloadTimeRanges();

    void buttonValidEndClick(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void buttonValidWeekClick(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void buttonBackClick(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void buttonHeaderBackClick(std::string button);

    void unselectWeekDays(void *data);
    void unselectAllWeekDays(void *data);
    void headerWeekButtonClick(std::string bt);

    void createTimeSelectTypeList(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void selectTimeType(void *data);
    void showTimeSelection(void *data);
    void showWeekSelection(void *data, Evas_Object *edje_object, std::string emission, std::string source);

    void deleteTimeRange(const TimeRange &range);

public:
    ActivityScheduleScenarioView(Evas *evas, Evas_Object *parent);
    ~ActivityScheduleScenarioView();

    virtual void resetView();

    void setTimeRangeInfos(const TimeRangeInfos &tr, bool _cycle) { range_infos = tr; reloadTimeRanges(); cycle = _cycle; }

    sigc::signal<void, TimeRangeInfos &> buttonValidPressed;
};

#endif // ActivityScheduleScenarioView_H
