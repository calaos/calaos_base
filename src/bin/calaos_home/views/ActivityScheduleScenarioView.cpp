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

ActivityScheduleScenarioView::ActivityScheduleScenarioView(Evas *e, Evas_Object *parent):
        ActivityView(e, parent, "calaos/page/schedule_scenario"),
        schedule_list(NULL),
        month_list(NULL)
{
        setPartText("header.label", "Planification horaire");

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
        header = new GenlistItemScenarioHeader(evas, parent, "Mois de l'année");
        header->Append(month_list);

        item = new GenlistItemSimple(evas, parent, "Toute l'année", true, false, NULL, "check");
        item->Append(month_list);
        item_all = item;
        item_all->setSelected(true);

        item = new GenlistItemSimple(evas, parent, "Janvier", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Février", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Mars", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Avril", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Mai", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Juin", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Juillet", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Aout", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Septembre", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Octobre", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Novembre", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Décembre", true, false, NULL, "check");
        item->Append(month_list);
        items_months.push_back(item);

        header = new GenlistItemScenarioHeader(evas, parent, "Périodes prédéfinies");
        header->Append(month_list);

        item = new GenlistItemSimple(evas, parent, "Printemps", true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Eté", true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Automne", true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

        item = new GenlistItemSimple(evas, parent, "Hiver", true, false, NULL, "check");
        item->Append(month_list);
        items_periods.push_back(item);

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
        //------------TEST schedule list
        for (int i = 0;i < 5;i++)
        {
                GenlistItemScenarioScheduleTime *item = new GenlistItemScenarioScheduleTime(evas, parent);
                item->Append(schedule_list);
        }
        //----------------
}
