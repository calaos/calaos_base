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
#include "ActivityEditScenarioView.h"
#include "ApplicationMain.h"
#include "IOGenlistRoomGroup.h"
#include "GenlistItemSimple.h"
#include "GenlistItemSimpleHeader.h"
#include "GenlistItemScenarioAction.h"
#include "ActivityIntl.h"

ActivityEditScenarioView::ActivityEditScenarioView(Evas *_e, Evas_Object *_parent):
    ActivityView(_e, _parent, "calaos/page/edit_scenario"),
    pager_step(NULL),
    pageName(NULL),
    pageActions(NULL),
    current_wizstep(1),
    home_list(NULL),
    actions_list(NULL),
    current_step(0)
{
    setPartText("header.label", _("My new scenario"));

    addCallback("button.*", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::buttonPressed));

    pager_step = elm_naviframe_add(parent);
    evas_object_show(pager_step);
    Swallow(pager_step, "step.content");
}

ActivityEditScenarioView::~ActivityEditScenarioView()
{
    DELETE_NULL_FUNC(evas_object_del, pager_step);
}

void ActivityEditScenarioView::resetView()
{
}

void ActivityEditScenarioView::setScenarioData(ScenarioData &data)
{
    scenario_data = data;

    cDebug() << "SCENARIO is empty? :" << scenario_data.toString();

    if (scenario_data.empty)
        scenario_data.name = _("My new scenario");

    if (scenario_data.steps.size() == 0)
    {
        //If empty, create at least one default step
        ScenarioStep s;
        scenario_data.steps.push_back(s);
    }

    showStep(current_wizstep);

    EmitSignal("set,step1", "calaos");
}

void ActivityEditScenarioView::showStep(int step)
{
    if (step == 1)
    {
        if (!pageName)
        {
            pageName = new EdjeObject(ApplicationMain::getTheme(), evas);
            pageName->LoadEdje("calaos/scenario/step_1");
            pageName->Show();
            pageName->setAutoDelete(true);
            pageName->object_deleted.connect(sigc::mem_fun(*this, &ActivityEditScenarioView::pageNameDeleted));
            pageName->addCallback("button.name", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::pageNameEditName));
            pageName->addCallback("button.*selected", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::pageNameVisiblePressed));
            pageName->setPartText("name.text", string(_("Scenario name: ")) + "<blue>" + scenario_data.name + "</blue>");
            updateVisibility();
        }
        elm_naviframe_item_push(pager_step, NULL, NULL, NULL, pageName->getEvasObject(), "calaos");
    }
    else if (step == 2)
    {
        if (!pageActions)
        {
            pageActions = new EdjeObject(ApplicationMain::getTheme(), evas);
            pageActions->LoadEdje("calaos/scenario/step_2");
            pageActions->Show();
            pageActions->setAutoDelete(true);
            pageActions->object_deleted.connect(sigc::mem_fun(*this, &ActivityEditScenarioView::pageActionsDeleted));
            pageActions->addCallback("button.step", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::buttonStepPressed));
            pageActions->addCallback("button.step.add", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::buttonStepAddPressed));
            pageActions->addCallback("button.step.delete", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::buttonStepDelPressed));
            pageActions->addCallback("button.*selected", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::pageActionsCyclePressed));
            updateCycling();

            //Load page Actions and items
            loadPageActions();
        }
        elm_naviframe_item_push(pager_step, NULL, NULL, NULL, pageActions->getEvasObject(), "calaos");
    }
}

void ActivityEditScenarioView::updateVisibility()
{
    if (scenario_data.visible)
    {
        pageName->EmitSignal("visible.select", "calaos");
        string r = "??";
        if (scenario_data.room) r = scenario_data.room->name;
        pageName->setPartText("visible.text", string(_("Displayed in the interface: ")) + "<blue>" + _("Enabled in") + "\"" + r + "\"</blue>");
    }
    else
    {
        pageName->EmitSignal("visible.unselect", "calaos");
        pageName->setPartText("visible.text", string(_("Displayed in the interface: ")) + "<blue>" + _("Don't display it.") + "</blue>");
    }
}

void ActivityEditScenarioView::updateCycling()
{
    if (scenario_data.params["cycle"] == "true")
    {
        pageActions->EmitSignal("cycle.select", "calaos");
        pageActions->setPartText("cycle.text", string(_("Infinite loop: ")) + "<blue>" + _("Enabled") + "</blue>");
    }
    else
    {
        pageActions->EmitSignal("cycle.unselect", "calaos");
        pageActions->setPartText("cycle.text", string(_("Infinite loop: ")) + "<blue>" + _("Disabled") + "</blue>");
    }
}

void ActivityEditScenarioView::pageNameEditName(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (!pageName) return;

    ApplicationMain::Instance().ShowKeyboard(_("Enter the name of scenario"),
                                             sigc::mem_fun(*this, &ActivityEditScenarioView::pageNameEditName_cb),
                                             false,
                                             scenario_data.name);
}

void ActivityEditScenarioView::pageNameEditName_cb(string text)
{
    scenario_data.name = text;
    if (pageName)
        pageName->setPartText("name.text", "Nom du scénario: <blue>" + scenario_data.name + "</blue>");
}

void ActivityEditScenarioView::pageNameVisiblePressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    Evas_Object *table = createPaddingTable(evas, parent, 280, 255);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label = "Visibilité du scénario<br><small><light_blue>Choisissez une pièce.</light_blue></small>";
    GenlistItemSimpleHeader *header = new GenlistItemSimpleHeader(evas, glist, title_label);
    header->Append(glist);

    GenlistItemSimple *item;

    item = new GenlistItemSimple(evas, parent, _("Don't display it"), true, false, NULL, "check");
    item->Append(glist);
    if (!scenario_data.room)
        item->setSelected(true);
    item->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::itemRoomSelected), (Room *)NULL));

    list<Room *>::iterator it = CalaosModel::Instance().getHome()->rooms.begin();
    for (;it != CalaosModel::Instance().getHome()->rooms.end();it++)
    {
        Room *room = *it;
        item = new GenlistItemSimple(evas, parent, room->name, true, false, NULL, "check");
        item->Append(glist);
        if (scenario_data.room == room)
            item->setSelected(true);
        else
            item->setSelected(false);
        item->setIcon("calaos/icons/genlist/room");
        item->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::itemRoomSelected), room));
    }

    elm_table_pack(table, glist, 1, 1, 1, 1);

    popup = elm_ctxpopup_add(parent);
    elm_object_content_set(popup, table);
    elm_object_style_set(popup, "calaos");
    elm_ctxpopup_direction_priority_set(popup,
                                        ELM_CTXPOPUP_DIRECTION_LEFT,
                                        ELM_CTXPOPUP_DIRECTION_RIGHT,
                                        ELM_CTXPOPUP_DIRECTION_UP,
                                        ELM_CTXPOPUP_DIRECTION_DOWN);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup, x, y);
    evas_object_show(popup);
}

void ActivityEditScenarioView::itemRoomSelected(void *data, Room *room)
{
    scenario_data.room = room;
    if (!room)
        scenario_data.visible = false;
    else
        scenario_data.visible = true;

    updateVisibility();

    elm_ctxpopup_dismiss(popup);
}

void ActivityEditScenarioView::pageNameDeleted()
{
    pageName = NULL;
}

void ActivityEditScenarioView::pageActionsDeleted()
{
    pageActions = NULL;
    DELETE_NULL_FUNC(evas_object_del, home_list);
    DELETE_NULL_FUNC(evas_object_del, actions_list);
}

void ActivityEditScenarioView::pageActionsCyclePressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (scenario_data.params["cycle"] == "true")
        scenario_data.params.Add("cycle", "false");
    else
        scenario_data.params.Add("cycle", "true");
    updateCycling();
}

void ActivityEditScenarioView::buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    if (source == "button.next")
    {
        if (current_wizstep >= 2) return;

        current_wizstep++;

        string _t = "set,step" + Utils::to_string(current_wizstep);
        EmitSignal(_t, "calaos");

        showStep(current_wizstep);

        buttonNextPressed.emit();
    }
    else if (source == "button.previous")
    {
        if (current_wizstep <= 1) return;

        current_wizstep--;

        string _t = "set,step" + Utils::to_string(current_wizstep);
        EmitSignal(_t, "calaos");

        elm_naviframe_item_pop(pager_step);

        buttonPreviousPressed.emit();
    }
    else if (source == "button.cancel")
    {
        buttonCancelPressed.emit();
    }
    else if (source == "button.valid")
    {
        buttonValidPressed.emit();
    }
}

void ActivityEditScenarioView::loadPageActions()
{
    //Create genlists
    home_list = elm_genlist_add(parent);
    pageActions->Swallow(home_list, "home.list");
    elm_genlist_homogeneous_set(home_list, true);
    elm_object_style_set(home_list, "calaos");
    evas_object_show(home_list);

    actions_list = elm_genlist_add(parent);
    pageActions->Swallow(actions_list, "actions.list");
    elm_object_style_set(actions_list, "calaos");
    evas_object_show(actions_list);

    list<Room *>::iterator it = CalaosModel::Instance().getHome()->rooms.begin();
    for (;it != CalaosModel::Instance().getHome()->rooms.end();it++)
    {
        Room *r = *it;
        IOGenlistRoomGroupIcon *room = new IOGenlistRoomGroupIcon(evas, home_list, r, "");
        room->Append(home_list);

        list<IOBase *>::iterator itio = r->scenario_ios.begin();
        for (;itio != r->scenario_ios.end();itio++)
        {
            IOBase *io = *itio;

            GenlistItemSimple *item = new GenlistItemSimple(evas, home_list, io->params["name"], false);
            item->Append(home_list);
            item->setIcon(io->getIconForIO());
            item->setButtonIcon("calaos/icons/action_button/more");
            item->button_pressed.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::actionAddPressed), io));
        }
    }

    loadActionsStep();
}

void ActivityEditScenarioView::actionAddPressed(IOBase *io)
{
    map<Room *, GenlistItemBase *>::iterator it;
    it = room_table.find(io->getRoom());

    ScenarioAction sa;
    IOActionList ac = io->getActionFromState();
    sa.io = io;
    sa.action = ac.getComputedAction(io);

    if (current_step == ScenarioData::END_STEP)
        scenario_data.step_end.actions.push_back(sa);
    else
        scenario_data.steps[current_step].actions.push_back(sa);

    GenlistItemScenarioAction *item = new GenlistItemScenarioAction(evas, parent,
                                                                    scenario_data,
                                                                    current_step,
                                                                    (current_step == ScenarioData::END_STEP)?
                                                                        scenario_data.step_end.actions.size() - 1:
                                                                        scenario_data.steps[current_step].actions.size() - 1);
    item->setAction(ac);
    item->delete_action.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::actionDelete),
                                           (current_step == ScenarioData::END_STEP)?
                                               scenario_data.step_end.actions.size() - 1:
                                               scenario_data.steps[current_step].actions.size() - 1,
                                           io->getRoom())); //click on "delete this action"

    if (it == room_table.end())
    {
        IOGenlistRoomGroupIcon *groom = new IOGenlistRoomGroupIcon(evas, actions_list, io->getRoom(), "");
        groom->Append(actions_list);

        item->Append(actions_list);
        room_table[io->getRoom()] = dynamic_cast<GenlistItemBase *>(item);
    }
    else
    {
        item->InsertAfter(actions_list, it->second);
        room_table[io->getRoom()] = dynamic_cast<GenlistItemBase *>(item);
    }

    item->BringInItem(ELM_GENLIST_ITEM_SCROLLTO_MIDDLE);
}

bool ScRoomCompare(const ScenarioAction lhs, const ScenarioAction rhs)
{
    return ((ScenarioAction)lhs).io->getRoom()->name > ((ScenarioAction)rhs).io->getRoom()->name &&
            ((ScenarioAction)lhs).io->getRoom()->type > ((ScenarioAction)rhs).io->getRoom()->type;
}

void ActivityEditScenarioView::loadActionsStep()
{
    if (current_step != ScenarioData::END_STEP)
    {
        if (current_step >= (int)scenario_data.steps.size())
            current_step = (int)scenario_data.steps.size() - 1;
        if (current_step < 0)
            current_step = 0;
    }

    room_table.clear();
    elm_genlist_clear(actions_list);

    if (edje_object_part_exists(pageActions->getEvasObject(), "button.step"))
    {
        Evas_Object *button = edje_object_part_external_object_get(pageActions->getEvasObject(), "button.step");
        string _t = (current_step == ScenarioData::END_STEP)? _("Final step") : string(_("Step")) + " " + Utils::to_string(current_step + 1);
        elm_object_text_set(button, _t.c_str());
    }

    ScenarioStep &step = (current_step == ScenarioData::END_STEP)? scenario_data.step_end:scenario_data.steps[current_step];
    std::sort(step.actions.begin(), step.actions.end(), ScRoomCompare);

    Room *r = NULL;
    IOGenlistRoomGroupIcon *groom = NULL;
    GenlistItemBase *base = NULL;

    for (uint i = 0;i < step.actions.size();i++)
    {
        ScenarioAction &sa = step.actions[i];
        IOBase *io = sa.io;

        if (r != io->getRoom())
        {
            r = io->getRoom();
            groom = new IOGenlistRoomGroupIcon(evas, actions_list, r, "");
            groom->Append(actions_list);
        }

        GenlistItemScenarioAction *item = new GenlistItemScenarioAction(evas, parent, scenario_data, current_step, i);
        IOActionList ac = io->getActionListFromAction(sa.action);
        item->setAction(ac);
        item->Append(actions_list);
        item->delete_action.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::actionDelete), i, r)); //click on "delete this action"
        base = dynamic_cast<GenlistItemBase *>(item);
        room_table[r] = base;
    }
}

void ActivityEditScenarioView::actionDelete(GenlistItemScenarioAction *item, void *data, int it, Room *room)
{
    ScenarioStep &step = (current_step == ScenarioData::END_STEP)? scenario_data.step_end:scenario_data.steps[current_step];
    step.actions.erase(step.actions.begin() + it);

    GenlistItemBase *prev = item->getPreviousItem();
    GenlistItemBase *next = item->getNextItem();

    IOGenlistRoomGroupIcon *rprev = dynamic_cast<IOGenlistRoomGroupIcon *>(prev);
    IOGenlistRoomGroupIcon *rnext = dynamic_cast<IOGenlistRoomGroupIcon *>(next);
    GenlistItemScenarioAction *aprev = dynamic_cast<GenlistItemScenarioAction *>(prev);

    if ((rprev && rnext) || //previous and next item are room header, so there is no more item in this room
        (rprev && !next))
    {
        rprev->RemoveItem();
        //delete room cache
        room_table.erase(rprev->getRoom());
    }
    if (aprev && rnext) //previous is an action and next is a room, change cache to the previous item
    {
        room_table[room] = aprev;
    }
    item->RemoveItem();
}

void ActivityEditScenarioView::buttonStepPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    Evas_Object *table = createPaddingTable(evas, parent, 300, 260);

    pager_step_popup = elm_naviframe_add(parent);
    evas_object_show(pager_step_popup);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label = string(_("Scenario step")) + "<br><small><light_blue>" + string(_("Progress of your scenario")) + "</light_blue></small>";
    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
    header->Append(glist);

    GenlistItemSimple *item_show = NULL;
    for (uint i = 0;i < scenario_data.steps.size();i++)
    {
        ScenarioStep &step = scenario_data.steps[i];

        GenlistItemSimple *item = new GenlistItemSimple(evas, parent, string(_("Step")) + " " + Utils::to_string(i + 1), true, false, NULL, "check");
        item->Append(glist, header);
        if ((int)i == current_step)
        {
            item->setSelected(true);
            item_show = item;
        }
        item->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::stepSelect), i));

        string _t = "Pause: " + time2string(step.pause / 1000, step.pause % 1000);
        GenlistItemSimple *item_pause = new GenlistItemSimple(evas, parent, _t, true, false, NULL, "disclosure");
        item_pause->Append(glist, header);
        item_pause->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::stepPauseChange), i, item_pause));
    }

    GenlistItemSimple *item_end = new GenlistItemSimple(evas, parent, _("Final step"), true, false, NULL, "check");
    item_end->Append(glist, header);
    if (current_step == ScenarioData::END_STEP)
    {
        item_end->setSelected(true);
        item_show = item_end;
    }
    item_end->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::stepSelect), ScenarioData::END_STEP));

    elm_table_pack(table, glist, 1, 1, 1, 1);

    step_popup = elm_ctxpopup_add(parent);
    elm_object_content_set(step_popup, pager_step_popup);
    elm_object_style_set(step_popup, "calaos");
    elm_ctxpopup_direction_priority_set(step_popup,
                                        ELM_CTXPOPUP_DIRECTION_DOWN,
                                        ELM_CTXPOPUP_DIRECTION_LEFT,
                                        ELM_CTXPOPUP_DIRECTION_UP,
                                        ELM_CTXPOPUP_DIRECTION_RIGHT);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(step_popup, x, y);
    evas_object_show(step_popup);

    elm_naviframe_item_push(pager_step_popup, NULL, NULL, NULL, table, "calaos");

    if (item_show)
        item_show->BringInItem(ELM_GENLIST_ITEM_SCROLLTO_MIDDLE);
}

void ActivityEditScenarioView::stepSelect(void *data, int step)
{
    current_step = step;
    loadActionsStep();
    elm_ctxpopup_dismiss(step_popup);
}

void ActivityEditScenarioView::stepPauseChange(void *data, int step, GenlistItemSimple *item)
{
    EdjeObject *page = new EdjeObject(ApplicationMain::getTheme(), evas);
    page->LoadEdje("calaos/popup/page/time");
    page->setAutoDelete(true);
    page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &ActivityEditScenarioView::buttonPauseBackClick));
    page->addCallback("button.valid", "pressed", sigc::bind(sigc::mem_fun(*this, &ActivityEditScenarioView::buttonPauseValidTimeClick), step, item));
    string t = "<b>" + string(_("Choose a time")) + "</b><br><light_blue><small>" + string(_("Paused time before the next step")) + "</small></light_blue>";
    page->setPartText("text", t);

    if (edje_object_part_exists(page->getEvasObject(), "button.back"))
    {
        Evas_Object *button = edje_object_part_external_object_get(page->getEvasObject(), "button.back");
        elm_object_text_set(button, "Etapes");
    }

    long pause = scenario_data.steps[step].pause;

    double sec = (double)pause / 1000.0;
    long ms = pause % 1000;
    int hours = (int)(sec / 3600.0);
    sec -= hours * 3600;
    int min = (int)(sec / 60.0);
    sec -= min * 60;

    spin_hours = elm_spinner_add(parent);
    elm_object_style_set(spin_hours, "calaos/time/vertical");
    elm_spinner_label_format_set(spin_hours, _("%.0f<br><subtitle>Hours</subtitle>"));
    elm_spinner_min_max_set(spin_hours, 0, 99);
    elm_spinner_step_set(spin_hours, 1);
    elm_spinner_interval_set(spin_hours, 0.15);
    elm_spinner_value_set(spin_hours, hours);
    evas_object_show(spin_hours);
    page->Swallow(spin_hours, "spinner.hours", true);

    spin_min = elm_spinner_add(parent);
    elm_object_style_set(spin_min, "calaos/time/vertical");
    elm_spinner_label_format_set(spin_min, _("%.0f<br><subtitle>Min.</subtitle>"));
    elm_spinner_min_max_set(spin_min, 0, 59);
    elm_spinner_step_set(spin_min, 1);
    elm_spinner_interval_set(spin_min, 0.15);
    elm_spinner_value_set(spin_min, min);
    evas_object_show(spin_min);
    page->Swallow(spin_min, "spinner.minutes", true);

    spin_sec = elm_spinner_add(parent);
    elm_object_style_set(spin_sec, "calaos/time/vertical");
    elm_spinner_label_format_set(spin_sec, _("%.0f<br><subtitle>Sec.</subtitle>"));
    elm_spinner_min_max_set(spin_sec, 0, 59);
    elm_spinner_step_set(spin_sec, 1);
    elm_spinner_interval_set(spin_sec, 0.15);
    elm_spinner_value_set(spin_sec, sec);
    evas_object_show(spin_sec);
    page->Swallow(spin_sec, "spinner.seconds", true);

    spin_ms = elm_spinner_add(parent);
    elm_object_style_set(spin_ms, "calaos/time/vertical");
    elm_spinner_label_format_set(spin_ms, _("%.0f<br><subtitle>Ms.</subtitle>"));
    elm_spinner_min_max_set(spin_ms, 0, 999);
    elm_spinner_step_set(spin_ms, 1);
    elm_spinner_interval_set(spin_ms, 0.15);
    elm_spinner_value_set(spin_ms, ms);
    evas_object_show(spin_ms);
    page->Swallow(spin_ms, "spinner.miliseconds", true);

    elm_naviframe_item_push(pager_step_popup, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void ActivityEditScenarioView::buttonPauseBackClick(void *data, Evas_Object *edje_object, string emission, string source)
{
    elm_naviframe_item_pop(pager_step_popup);
}

void ActivityEditScenarioView::buttonPauseValidTimeClick(void *data, Evas_Object *edje_object, string emission, string source, int step, GenlistItemSimple *item)
{
    scenario_data.steps[step].pause = elm_spinner_value_get(spin_hours) * 60.0 * 60.0 * 1000.0 +
                                      elm_spinner_value_get(spin_min) * 60.0 * 1000.0 +
                                      elm_spinner_value_get(spin_sec) * 1000.0 +
                                      elm_spinner_value_get(spin_ms);

    string _t = "Pause: " + time2string(scenario_data.steps[step].pause / 1000, scenario_data.steps[step].pause % 1000);
    item->setLabelText(_t);
    elm_naviframe_item_pop(pager_step_popup);
}

void ActivityEditScenarioView::buttonStepAddPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    ScenarioStep step;
    scenario_data.steps.push_back(step);

    current_step = scenario_data.steps.size() - 1;
    loadActionsStep();
}

void ActivityEditScenarioView::buttonStepDelPressed(void *data, Evas_Object *_edje, std::string emission, std::string source)
{
    Evas_Object *table = createPaddingTable(evas, parent, 300, 130);

    Evas_Object *glist = elm_genlist_add(table);
    elm_object_style_set(glist, "calaos");
    elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
    evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
    evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_show(glist);

    string title_label = string(_("Confirmation")) + "<br><small><light_blue>" + string(_("Are you sure to want deleting this step?")) + "</light_blue></small>";
    GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
    header->Append(glist);

    GenlistItemSimple *item = new GenlistItemSimple(evas, parent, _("Yes, del this step"), true);
    item->Append(glist, header);
    item->item_selected.connect(sigc::mem_fun(*this, &ActivityEditScenarioView::deleteStepValid));

    item = new GenlistItemSimple(evas, parent, _("No"), true);
    item->Append(glist, header);
    item->item_selected.connect(sigc::mem_fun(*this, &ActivityEditScenarioView::deleteStepCancel));

    elm_table_pack(table, glist, 1, 1, 1, 1);

    popup_del = elm_ctxpopup_add(parent);
    elm_object_content_set(popup_del, table);
    elm_object_style_set(popup_del, "calaos");
    elm_ctxpopup_direction_priority_set(popup_del,
                                        ELM_CTXPOPUP_DIRECTION_DOWN,
                                        ELM_CTXPOPUP_DIRECTION_LEFT,
                                        ELM_CTXPOPUP_DIRECTION_UP,
                                        ELM_CTXPOPUP_DIRECTION_RIGHT);

    Evas_Coord x,y;
    evas_pointer_canvas_xy_get(evas, &x, &y);
    evas_object_move(popup_del, x, y);
    evas_object_show(popup_del);
}

void ActivityEditScenarioView::deleteStepValid(void *data)
{
    if (current_step == ScenarioData::END_STEP)
    {
        scenario_data.step_end = ScenarioStep();
    }
    else
    {
        if (current_step < 0 || current_step >= (int)scenario_data.steps.size())
            return;

        if (scenario_data.steps.size() > 1)
            scenario_data.steps.erase(scenario_data.steps.begin() + current_step);
        else
            scenario_data.steps[0] = ScenarioStep();

        if (current_step >= (int)scenario_data.steps.size())
            current_step = scenario_data.steps.size() - 1;
    }

    loadActionsStep();
    elm_ctxpopup_dismiss(popup_del);
}

void ActivityEditScenarioView::deleteStepCancel(void *data)
{
    elm_ctxpopup_dismiss(popup_del);
}

