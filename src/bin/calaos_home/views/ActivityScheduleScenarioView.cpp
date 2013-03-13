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

ActivityScheduleScenarioView::ActivityScheduleScenarioView(Evas *_e, Evas_Object *_parent):
        ActivityView(_e, _parent, "calaos/page/schedule_scenario"),
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
        else if (source == "button.add")
        {
                pager_popup = elm_naviframe_add(parent);
                evas_object_show(pager_popup);

                EdjeObject *page = new EdjeObject(ApplicationMain::getTheme(), evas);
                page->LoadEdje("calaos/popup/page/time");
                page->setAutoDelete(true);
                if (cycle)
                        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonValidEndClick));
                else
                        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonValidWeekClick));
                string t = "<b>Choisir une heure de planification</b><br><light_blue><small>Heure de départ du scénario</small></light_blue>";
                page->setPartText("text", t);

                page->EmitSignal("hide,back", "calaos");

                spin_start_hours = elm_spinner_add(parent);
                elm_object_style_set(spin_start_hours, "calaos/time/vertical");
                elm_spinner_label_format_set(spin_start_hours, "%.0f<br><subtitle>Heures</subtitle>");
                elm_spinner_min_max_set(spin_start_hours, 0, 99);
                elm_spinner_step_set(spin_start_hours, 1);
                elm_spinner_interval_set(spin_start_hours, 0.15);
                elm_spinner_value_set(spin_start_hours, 12);
                evas_object_show(spin_start_hours);
                page->Swallow(spin_start_hours, "spinner.hours", true);

                spin_start_min = elm_spinner_add(parent);
                elm_object_style_set(spin_start_min, "calaos/time/vertical");
                elm_spinner_label_format_set(spin_start_min, "%.0f<br><subtitle>Min.</subtitle>");
                elm_spinner_min_max_set(spin_start_min, 0, 59);
                elm_spinner_step_set(spin_start_min, 1);
                elm_spinner_interval_set(spin_start_min, 0.15);
                elm_spinner_value_set(spin_start_min, 0);
                evas_object_show(spin_start_min);
                page->Swallow(spin_start_min, "spinner.minutes", true);

                spin_start_sec = elm_spinner_add(parent);
                elm_object_style_set(spin_start_sec, "calaos/time/vertical");
                elm_spinner_label_format_set(spin_start_sec, "%.0f<br><subtitle>Sec.</subtitle>");
                elm_spinner_min_max_set(spin_start_sec, 0, 59);
                elm_spinner_step_set(spin_start_sec, 1);
                elm_spinner_interval_set(spin_start_sec, 0.15);
                elm_spinner_value_set(spin_start_sec, 0);
                evas_object_show(spin_start_sec);
                page->Swallow(spin_start_sec, "spinner.seconds", true);

                spin_start_ms = elm_spinner_add(parent);
                elm_object_style_set(spin_start_ms, "calaos/time/vertical");
                elm_spinner_label_format_set(spin_start_ms, "%.0f<br><subtitle>Ms.</subtitle>");
                elm_spinner_min_max_set(spin_start_ms, 0, 999);
                elm_spinner_step_set(spin_start_ms, 1);
                elm_spinner_interval_set(spin_start_ms, 0.15);
                elm_spinner_value_set(spin_start_ms, 0);
                evas_object_show(spin_start_ms);
                page->Swallow(spin_start_ms, "spinner.miliseconds", true);

                evas_object_size_hint_min_set(page->getEvasObject(), 300, 260);
                page->Show();

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

                elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, page->getEvasObject(), "calaos");
        }
}

void ActivityScheduleScenarioView::buttonValidEndClick(void *data, Evas_Object *edje_object, string emission, string source)
{
        //Ask for an end hour if the scenario is cycling
        EdjeObject *page = new EdjeObject(ApplicationMain::getTheme(), evas);
        page->LoadEdje("calaos/popup/page/time");
        page->setAutoDelete(true);
        page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonBackClick));
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &ActivityScheduleScenarioView::buttonValidWeekClick));
        string t = "<b>Choisir une heure de planification</b><br><light_blue><small>Heure d'arrêt du scénario</small></light_blue>";
        page->setPartText("text", t);

        if (edje_object_part_exists(page->getEvasObject(), "button.back"))
        {
                Evas_Object *button = edje_object_part_external_object_get(page->getEvasObject(), "button.back");
                elm_object_text_set(button, "Début");
        }

        spin_end_hours = elm_spinner_add(parent);
        elm_object_style_set(spin_end_hours, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_end_hours, "%.0f<br><subtitle>Heures</subtitle>");
        elm_spinner_min_max_set(spin_end_hours, 0, 99);
        elm_spinner_step_set(spin_end_hours, 1);
        elm_spinner_interval_set(spin_end_hours, 0.15);
        elm_spinner_value_set(spin_end_hours, 13);
        evas_object_show(spin_end_hours);
        page->Swallow(spin_end_hours, "spinner.hours", true);

        spin_end_min = elm_spinner_add(parent);
        elm_object_style_set(spin_end_min, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_end_min, "%.0f<br><subtitle>Min.</subtitle>");
        elm_spinner_min_max_set(spin_end_min, 0, 59);
        elm_spinner_step_set(spin_end_min, 1);
        elm_spinner_interval_set(spin_end_min, 0.15);
        elm_spinner_value_set(spin_end_min, 0);
        evas_object_show(spin_end_min);
        page->Swallow(spin_end_min, "spinner.minutes", true);

        spin_end_sec = elm_spinner_add(parent);
        elm_object_style_set(spin_end_sec, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_end_sec, "%.0f<br><subtitle>Sec.</subtitle>");
        elm_spinner_min_max_set(spin_end_sec, 0, 59);
        elm_spinner_step_set(spin_end_sec, 1);
        elm_spinner_interval_set(spin_end_sec, 0.15);
        elm_spinner_value_set(spin_end_sec, 0);
        evas_object_show(spin_end_sec);
        page->Swallow(spin_end_sec, "spinner.seconds", true);

        spin_end_ms = elm_spinner_add(parent);
        elm_object_style_set(spin_end_ms, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_end_ms, "%.0f<br><subtitle>Ms.</subtitle>");
        elm_spinner_min_max_set(spin_end_ms, 0, 999);
        elm_spinner_step_set(spin_end_ms, 1);
        elm_spinner_interval_set(spin_end_ms, 0.15);
        elm_spinner_value_set(spin_end_ms, 0);
        evas_object_show(spin_end_ms);
        page->Swallow(spin_end_ms, "spinner.miliseconds", true);

        evas_object_size_hint_min_set(page->getEvasObject(), 300, 260);
        page->Show();

        elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void ActivityScheduleScenarioView::buttonValidWeekClick(void *data, Evas_Object *edje_object, string emission, string source)
{
        Evas_Object *table = createPaddingTable(evas, parent, 300, 260);

        Evas_Object *glist = elm_genlist_add(table);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        elm_genlist_multi_select_set(glist, true);
        evas_object_show(glist);

        string title_label = "Jours de la semaine<br><small><light_blue>Jours de la semaine où le scénario sera exécuté.</light_blue></small>";
        GenlistItemSimpleHeader *header = new GenlistItemSimpleHeader(evas, glist, title_label, "navigation");
        header->Append(glist);

        if (cycle)
                header->setButtonLabel("button.back", "Fin");
        else
                header->setButtonLabel("button.back", "Début");
        header->button_click.connect(sigc::mem_fun(*this, &ActivityScheduleScenarioView::headerWeekButtonClick));

        week_days.clear();
        week_days.reserve(8);
        GenlistItemSimple *item;

        for (int i = 0;i < 8;i++)
        {
                string label;
                switch (i)
                {
                case 0: label = "Tous les jours"; break;
                case 1: label = "Lundi"; break;
                case 2: label = "Mardi"; break;
                case 3: label = "Mercredi"; break;
                case 4: label = "Jeudi"; break;
                case 5: label = "Vendredi"; break;
                case 6: label = "Samedi"; break;
                case 7: label = "Dimanche"; break;
                default: label = "ERROR";
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
                //TODO,add to TimeRange and update genlist

                elm_ctxpopup_dismiss(popup);
        }
}

void ActivityScheduleScenarioView::buttonBackClick(void *data, Evas_Object *edje_object, string emission, string source)
{
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
        //------------TEST schedule list
        for (int i = 0;i < 5;i++)
        {
                GenlistItemScenarioScheduleTime *item = new GenlistItemScenarioScheduleTime(evas, parent);
                item->Append(schedule_list);
        }
        //----------------
}
