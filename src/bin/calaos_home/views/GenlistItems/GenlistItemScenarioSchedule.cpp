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

#include "GenlistItemScenarioSchedule.h"
#include <ApplicationMain.h>

#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>

ITEM_BUTTON_CALLBACK(GenlistItemScenarioSchedule, More)

GenlistItemScenarioSchedule::GenlistItemScenarioSchedule(Evas *_evas, Evas_Object *_parent, bool scheduleView, Scenario *sc, void *data):
    GenlistItemBase(_evas, _parent, (scheduleView)?"scenario/schedule":"scenario/standard", ELM_GENLIST_ITEM_NONE, data),
    IOBaseElement(sc->ioScenario),
    scenario(sc)
{
    CalaosModel::Instance().getScenario()->scenario_change.connect([=](Scenario *s)
    {
        if (scenario == s) updateView();
    });
}

GenlistItemScenarioSchedule::~GenlistItemScenarioSchedule()
{
}

void GenlistItemScenarioSchedule::ioDeleted()
{
    IOBaseElement::ioDeleted();

    DELETE_NULL_FUNC(elm_object_item_del, item);
}

void GenlistItemScenarioSchedule::initView()
{
    updateView();
}

void GenlistItemScenarioSchedule::updateView()
{
    if (!io) return;

    if (scenario->ioScenario)
    {
        elm_genlist_item_fields_update(item, "text", ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_genlist_item_fields_update(item, "time", ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_genlist_item_fields_update(item, "actions.text", ELM_GENLIST_ITEM_FIELD_TEXT);
    }
}

string GenlistItemScenarioSchedule::getLabelItem(Evas_Object *obj, string part)
{
    string text;

    if (!scenario) return text;

    if (part == "text")
    {
        text = scenario->ioScenario->params["name"];
    }
    else if (part == "time")
    {
        text = "N/A";

        if (scenario->isScheduled())
        {
            vector<TimeRange> v = scenario->ioSchedule->range_infos.getRange(scheduleRange);

            if (scheduleRangeNum >= 0 && scheduleRangeNum < (int)v.size())
            {
                TimeRange tr = v[scheduleRangeNum];
                cDebugDom("scenario") << "Get time for scenario: " << scenario->ioScenario->params["name"];
                text = Utils::time2string_digit(tr.getStartTimeSec(scDate.tm_year + 1900, scDate.tm_mon + 1, scDate.tm_mday));
                cDebugDom("scenario") << "time: " << text;
                cDebugDom("scenario") << "Schedule: ";
                cDebugDom("scenario") << scenario->ioSchedule->range_infos.toString();
            }
        }
    }
    else if (part == "actions.text")
    {
        text = _("No actions.");
        if (scenario->scenario_data.steps.size() > 1)
            text = Utils::to_string(scenario->scenario_data.steps.size()) + " " + _("steps") + ".";
        else if (scenario->scenario_data.steps.size() == 1 &&
                 scenario->scenario_data.steps[0].actions.size() > 0)
            text = Utils::to_string(scenario->scenario_data.steps[0].actions.size()) + " " + _("actions") + ".";
    }

    if (scenario->scenario_data.params["schedule"] != "false")
        itemEmitSignal("schedule,true", "calaos");
    else
        itemEmitSignal("schedule,false", "calaos");

    return text;
}

Evas_Object *GenlistItemScenarioSchedule::getPartItem(Evas_Object *obj, string part)
{
    Evas_Object *o = NULL;

    if (part == "calaos.button.more")
    {
        o = elm_button_add(parent);
        elm_object_style_set(o, "calaos/button/position/rolldown");
        evas_object_smart_callback_add(o, "clicked", _item_button_More, this);
    }

    return o;
}

void GenlistItemScenarioSchedule::buttonClickMore()
{
    if (!scenario) return;

    Evas_Object *table = createPaddingTable(evas, parent, 280, 260);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label = _("Scenario parameters<br><small><light_blue>Modify, add or delete a schedule.</light_blue></small>");
    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
    header->Append(glist);

    GenlistItemSimple *_item;

    _item = new GenlistItemSimple(evas, glist, _("Modify scenario"), true);
    _item->setIcon("calaos/icons/genlist/edit");
    _item->Append(glist, header);
    _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scenarioModify));

    _item = new GenlistItemSimple(evas, glist, _("Delete scenario"), true);
    _item->setIcon("calaos/icons/genlist/trash");
    _item->Append(glist, header);
    _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scenarioDelete));

    _item = new GenlistItemSimple(evas, glist, _("Run scenario now"), true);
    _item->setIcon("calaos/icons/genlist/play");
    _item->Append(glist, header);
    _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scenarioPlay));

    if (scenario->scenario_data.params["schedule"] == "false")
    {
        _item = new GenlistItemSimple(evas, glist, _("Add a schedule"), true);
        _item->setIcon("calaos/icons/genlist/plus");
        _item->Append(glist, header);
        _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scheduleAdd));
    }
    else
    {
        _item = new GenlistItemSimple(evas, glist, _("Schedule modification"), true);
        _item->setIcon("calaos/icons/genlist/clock");
        _item->Append(glist, header);
        _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scheduleModify));

        _item = new GenlistItemSimple(evas, glist, _("Delete schedule"), true);
        _item->setIcon("calaos/icons/genlist/trash");
        _item->Append(glist, header);
        _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::scheduleDelete));
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    pager_popup = elm_naviframe_add(parent);
    evas_object_show(pager_popup);

    popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, pager_popup);
    elm_object_style_set(popup, "calaos");
    elm_ctxpopup_direction_priority_set(popup,
                                        ELM_CTXPOPUP_DIRECTION_DOWN,
                                        ELM_CTXPOPUP_DIRECTION_UP,
                                        ELM_CTXPOPUP_DIRECTION_LEFT,
                                        ELM_CTXPOPUP_DIRECTION_RIGHT);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);

    elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, table, "calaos");
}

void GenlistItemScenarioSchedule::scheduleAdd(void *data)
{
    schedule_add_click.emit(scenario);
    elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioSchedule::scheduleModify(void *data)
{
    schedule_modify_click.emit(scenario);
    elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioSchedule::scheduleDelete(void *data)
{
    schedule_del_click.emit(scenario);
    elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioSchedule::scenarioDelete(void *data)
{
    if (!scenario) return;

    Evas_Object *table = createPaddingTable(evas, parent, 280, 260);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label = _("Confirmation<br><small><light_blue>Are you sure to delete this scenarios?</light_blue></small>");
    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
    header->Append(glist);

    GenlistItemSimple *_item = new GenlistItemSimple(evas, parent, _("Yes, delete it"), true);
    _item->Append(glist, header);
    _item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::deleteScenarioValid));

    _item = new GenlistItemSimple(evas, parent, _("No"), true);
    _item->Append(glist, header);
    _item->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioSchedule::deleteScenarioCancel), _item));

    elm_table_pack(table, glist, 1, 1, 1, 1);

    elm_naviframe_item_push(pager_popup, NULL, NULL, NULL, table, "calaos");
}

void GenlistItemScenarioSchedule::deleteScenarioValid(void *data)
{
    if (!scenario) return;

    CalaosModel::Instance().getScenario()->deleteScenario(scenario);
    elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioSchedule::deleteScenarioCancel(void *data, GenlistItemSimple *cancel_item)
{
    if (!scenario) return;

    elm_naviframe_item_pop(pager_popup);
    cancel_item->setSelected(false);
}

void GenlistItemScenarioSchedule::scenarioModify(void *data)
{
    if (!scenario) return;

    ApplicationMain::Instance().ShowScenarioEditor(scenario);

    elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioSchedule::scenarioPlay(void *data)
{
    if (!scenario) return;

    scenario->ioScenario->sendAction("true");
    elm_ctxpopup_dismiss(popup);
}
