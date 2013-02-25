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

#include "GenlistItemScenarioAction.h"
#include <ApplicationMain.h>

#include <GenlistItemSimpleHeader.h>
#include <GenlistItemSimple.h>

ITEM_BUTTON_CALLBACK(GenlistItemScenarioAction, Edit)

GenlistItemScenarioAction::GenlistItemScenarioAction(Evas *_evas, Evas_Object *_parent, ScenarioData &scd, int step, int _action, void *data):
        GenlistItemBase(_evas, _parent, "scenario/action", ELM_GENLIST_ITEM_NONE, data),
        scenario_data(scd),
        sc_step(step),
        sc_action(_action)
{
        ScenarioAction &ac = getAction();
        action = ac.io->getActionFromState();
}

GenlistItemScenarioAction::~GenlistItemScenarioAction()
{
}

ScenarioAction &GenlistItemScenarioAction::getAction()
{
        if (sc_step == ScenarioData::END_STEP)
                return scenario_data.step_end.actions[sc_action];

        return scenario_data.steps[sc_step].actions[sc_action];
}

string GenlistItemScenarioAction::getLabelItem(Evas_Object *obj, string part)
{
        string text;

        if (part == "text")
        {
                ScenarioAction &ac = getAction();
                text = ac.io->params["name"];
        }
        else if (part == "action.text")
        {
                ScenarioAction &ac = getAction();
                text = action.getComputedTitle(ac.io);
        }

        return text;
}

Evas_Object *GenlistItemScenarioAction::getPartItem(Evas_Object *obj, string part)
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
        else if (part == "icon")
        {
                o = elm_icon_add(parent);
                ScenarioAction &ac = getAction();
                elm_image_file_set(o, ApplicationMain::getTheme(), ac.io->getIconForIO().c_str());
        }

        return o;
}

void GenlistItemScenarioAction::buttonClickEdit()
{
        Evas_Object *table = createPaddingTable(evas, parent, 280, 255);

        pager_action = elm_naviframe_add(parent);
        evas_object_show(pager_action);

        Evas_Object *glist = elm_genlist_add(table);
        elm_object_style_set(glist, "calaos");
        elm_genlist_select_mode_set(glist, ELM_OBJECT_SELECT_MODE_ALWAYS);
        evas_object_size_hint_fill_set(glist, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(glist, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_show(glist);

        string title_label = "Edition de l'élément<br><small><light_blue>Choisissez l'action qui sera executé.</light_blue></small>";
        GenlistItemBase *header = new GenlistItemSimpleHeader(evas, glist, title_label);
        header->Append(glist);

        GenlistItemSimple *del_item = new GenlistItemSimple(evas, glist, "Supprimer cette action", true);
        del_item->Append(glist, header);
        del_item->setIcon("calaos/icons/genlist/trash");
        del_item->item_selected.connect(sigc::mem_fun(*this, &GenlistItemScenarioAction::deleteItemSelected));

        createActionList(glist, header);

        elm_table_pack(table, glist, 1, 1, 1, 1);

        popup = elm_ctxpopup_add(parent);
        elm_object_content_set(popup, pager_action);
        elm_object_style_set(popup, "calaos");
        elm_ctxpopup_direction_priority_set(popup,
                                            ELM_CTXPOPUP_DIRECTION_LEFT,
                                            ELM_CTXPOPUP_DIRECTION_UP,
                                            ELM_CTXPOPUP_DIRECTION_DOWN,
                                            ELM_CTXPOPUP_DIRECTION_RIGHT);

        Evas_Coord x,y;
        evas_pointer_canvas_xy_get(evas, &x, &y);
        evas_object_move(popup, x, y);
        evas_object_show(popup);

        elm_naviframe_item_push(pager_action, NULL, NULL, NULL, table, "calaos");
}

void GenlistItemScenarioAction::deleteItemSelected(void *data)
{
        elm_ctxpopup_dismiss(popup);

        delete_action.emit(this, data);
}

void GenlistItemScenarioAction::createActionList(Evas_Object *glist, GenlistItemBase *header)
{
        GenlistItemSimple *it = NULL;

        ScenarioAction &ac_current = getAction();
        vector<IOActionList> v = ac_current.io->getActionList();

        for (uint i = 0;i < v.size();i++)
        {
                IOActionList &ac = v[i];
                ac.copyValueFrom(action);

                if (ac.type == IOActionList::ACTION_SIMPLE)
                        it = new GenlistItemSimple(evas, glist, ac.title, true);
                else
                        it = new GenlistItemSimple(evas, glist, ac.title, true, false, NULL, "disclosure");

                it->setIcon("calaos/icons/genlist/action");
                it->Append(glist, header);

                if (ac.type == IOActionList::ACTION_SIMPLE)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionSimple), ac));
                else if (ac.type == IOActionList::ACTION_SLIDER)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionSlider), ac));
                else if (ac.type == IOActionList::ACTION_NUMBER)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionNumber), ac));
                else if (ac.type == IOActionList::ACTION_TEXT)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionText), ac));
                else if (ac.type == IOActionList::ACTION_TIME_MS)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionTime), ac));
                else if (ac.type == IOActionList::ACTION_COLOR)
                        it->item_selected.connect(sigc::bind(sigc::mem_fun(*this, &GenlistItemScenarioAction::actionColor), ac));
        }
}

void GenlistItemScenarioAction::actionSimple(void *data, IOActionList ac)
{
        action = ac;
        if (sc_step == ScenarioData::END_STEP)
                scenario_data.step_end.actions[sc_action].action =
                                action.getComputedAction(scenario_data.step_end.actions[sc_action].io);
        else
                scenario_data.steps[sc_step].actions[sc_action].action =
                                action.getComputedAction(scenario_data.steps[sc_step].actions[sc_action].io);

        elm_genlist_item_fields_update(item, "action.text",  ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioAction::buttonBackClick(void *data, Evas_Object *edje_object, string emission, string source)
{
        elm_naviframe_item_pop(pager_action);
}

void GenlistItemScenarioAction::buttonValidClick(void *data, Evas_Object *edje_object, string emission, string source)
{
        action = action_temp;
        if (sc_step == ScenarioData::END_STEP)
                scenario_data.step_end.actions[sc_action].action =
                                action.getComputedAction(scenario_data.step_end.actions[sc_action].io);
        else
                scenario_data.steps[sc_step].actions[sc_action].action =
                                action.getComputedAction(scenario_data.steps[sc_step].actions[sc_action].io);

        elm_genlist_item_fields_update(item, "action.text",  ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioAction::buttonValidTimeClick(void *data, Evas_Object *edje_object, string emission, string source)
{
        action = action_temp;
        action.dvalue = elm_spinner_value_get(spin_hours) * 60.0 * 60.0 * 1000.0 +
                        elm_spinner_value_get(spin_min) * 60.0 * 1000.0 +
                        elm_spinner_value_get(spin_sec) * 1000.0 +
                        elm_spinner_value_get(spin_ms);

        elm_genlist_item_fields_update(item, "action.text",  ELM_GENLIST_ITEM_FIELD_TEXT);
        elm_ctxpopup_dismiss(popup);
}

void GenlistItemScenarioAction::actionSlider(void *data, IOActionList ac)
{
        action_temp = ac;

        page = new EdjeObject(ApplicationMain::getTheme(), evas);
        page->LoadEdje("calaos/popup/page/slider");
        page->setAutoDelete(true);
        page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonBackClick));
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonValidClick));
        page->addCallback("slider_obj:object", "*", sigc::mem_fun(*this, &GenlistItemScenarioAction::sliderSignalCallback));
        page->setDragValue("slider", action_temp.dvalue / 100.0, 0.0);
        string t = "<b>Choisir la valeur</b><br><light_blue><small>" + ac.title + "</small></light_blue>";
        page->setPartText("text", t);

        elm_naviframe_item_push(pager_action, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void GenlistItemScenarioAction::sliderSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        if (emission == "slider,changed")
        {
                double x;
                page->getDragValue("slider", &x, NULL);

                action_temp.dvalue = (int)(x * 100.0);
        }
}

void GenlistItemScenarioAction::actionNumber(void *data, IOActionList ac)
{
        action_temp = ac;

        page = new EdjeObject(ApplicationMain::getTheme(), evas);
        page->LoadEdje("calaos/popup/page/number");
        page->setAutoDelete(true);
        page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonBackClick));
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonValidClick));
        page->addCallback("button.pad.*", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::numberSignalCallback));
        string t = "<b>Choisir un nombre</b><br><light_blue><small>" + ac.title + "</small></light_blue>";
        page->setPartText("text", t);
        page->setPartText("value", to_string(action_temp.dvalue));

        elm_naviframe_item_push(pager_action, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void GenlistItemScenarioAction::numberSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        string val = page->getPartText("value");

        if (source == "button.pad.0") val += "0";
        else if (source == "button.pad.1") val += "1";
        else if (source == "button.pad.2") val += "2";
        else if (source == "button.pad.3") val += "3";
        else if (source == "button.pad.4") val += "4";
        else if (source == "button.pad.5") val += "5";
        else if (source == "button.pad.6") val += "6";
        else if (source == "button.pad.7") val += "7";
        else if (source == "button.pad.8") val += "8";
        else if (source == "button.pad.9") val += "9";
        else if (source == "button.pad.dot")
        {
                if (val.find('.') == string::npos)
                        val += ".";
        }
        else if (source == "button.pad.del")
        {
                if (val.length() > 0)
                        val.erase(val.end() - 1);
        }

        if (val.length() > 1 && val[val.length()] != '0')
                while (val[0] == '0') val.erase(val.begin());

        if (val.length() == 0)
                val = "0";

        page->setPartText("value", val);
        from_string(val, action_temp.dvalue);
}

void GenlistItemScenarioAction::actionText(void *data, IOActionList ac)
{
        action_temp = ac;

        //TODO!
}

void GenlistItemScenarioAction::actionTime(void *data, IOActionList ac)
{
        action_temp = ac;

        page = new EdjeObject(ApplicationMain::getTheme(), evas);
        page->LoadEdje("calaos/popup/page/time");
        page->setAutoDelete(true);
        page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonBackClick));
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonValidTimeClick));
        string t = "<b>Choisir une durée</b><br><light_blue><small>" + ac.title + "</small></light_blue>";
        page->setPartText("text", t);

        double sec = action_temp.dvalue / 1000.0;
        long ms = (long)action_temp.dvalue % 1000;
        int hours = (int)(sec / 3600.0);
        sec -= hours * 3600;
        int min = (int)(sec / 60.0);
        sec -= min * 60;

        spin_hours = elm_spinner_add(parent);
        elm_object_style_set(spin_hours, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_hours, "%.0f<br><subtitle>Heures</subtitle>");
        elm_spinner_min_max_set(spin_hours, 0, 99);
        elm_spinner_step_set(spin_hours, 1);
        elm_spinner_interval_set(spin_hours, 0.15);
        elm_spinner_value_set(spin_hours, hours);
        evas_object_show(spin_hours);
        page->Swallow(spin_hours, "spinner.hours", true);

        spin_min = elm_spinner_add(parent);
        elm_object_style_set(spin_min, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_min, "%.0f<br><subtitle>Min.</subtitle>");
        elm_spinner_min_max_set(spin_min, 0, 59);
        elm_spinner_step_set(spin_min, 1);
        elm_spinner_interval_set(spin_min, 0.15);
        elm_spinner_value_set(spin_min, min);
        evas_object_show(spin_min);
        page->Swallow(spin_min, "spinner.minutes", true);

        spin_sec = elm_spinner_add(parent);
        elm_object_style_set(spin_sec, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_sec, "%.0f<br><subtitle>Sec.</subtitle>");
        elm_spinner_min_max_set(spin_sec, 0, 59);
        elm_spinner_step_set(spin_sec, 1);
        elm_spinner_interval_set(spin_sec, 0.15);
        elm_spinner_value_set(spin_sec, sec);
        evas_object_show(spin_sec);
        page->Swallow(spin_sec, "spinner.seconds", true);

        spin_ms = elm_spinner_add(parent);
        elm_object_style_set(spin_ms, "calaos/time/vertical");
        elm_spinner_label_format_set(spin_ms, "%.0f<br><subtitle>Ms.</subtitle>");
        elm_spinner_min_max_set(spin_ms, 0, 999);
        elm_spinner_step_set(spin_ms, 1);
        elm_spinner_interval_set(spin_ms, 0.15);
        elm_spinner_value_set(spin_ms, ms);
        evas_object_show(spin_ms);
        page->Swallow(spin_ms, "spinner.miliseconds", true);

        elm_naviframe_item_push(pager_action, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void GenlistItemScenarioAction::actionColor(void *data, IOActionList ac)
{
        action_temp = ac;

        page = new EdjeObject(ApplicationMain::getTheme(), evas);
        page->LoadEdje("calaos/popup/page/color");
        page->setAutoDelete(true);
        page->addCallback("button.back", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonBackClick));
        page->addCallback("button.valid", "pressed", sigc::mem_fun(*this, &GenlistItemScenarioAction::buttonValidClick));
        page->addCallback("slider.red:object", "*", sigc::mem_fun(*this, &GenlistItemScenarioAction::sliderRedSignalCallback));
        page->addCallback("slider.green:object", "*", sigc::mem_fun(*this, &GenlistItemScenarioAction::sliderGreenSignalCallback));
        page->addCallback("slider.blue:object", "*", sigc::mem_fun(*this, &GenlistItemScenarioAction::sliderBlueSignalCallback));
        page->setDragValue("slider", action_temp.dvalue / 100.0, 0.0);
        string t = "<b>Choisir la couleur</b><br><light_blue><small>" + ac.title + "</small></light_blue>";
        page->setPartText("text", t);

        page->setDragValue("slider.red:slider", action_temp.red / 100.0, 0.0);
        page->setDragValue("slider.green:slider", action_temp.green / 100.0, 0.0);
        page->setDragValue("slider.blue:slider", action_temp.blue / 100.0, 0.0);

        color_preview = evas_object_rectangle_add(evas);
        evas_object_color_set(color_preview, action_temp.red, action_temp.green, action_temp.blue, 255);
        evas_object_show(color_preview);
        page->Swallow(color_preview, "color.preview", true);

        elm_naviframe_item_push(pager_action, NULL, NULL, NULL, page->getEvasObject(), "calaos");
}

void GenlistItemScenarioAction::sliderRedSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        if (emission == "slider,changed")
        {
                double x;
                page->getDragValue("slider.red:slider", &x, NULL);

                action_temp.red = (int)(x * 100.0);

                evas_object_color_set(color_preview, action_temp.red, action_temp.green, action_temp.blue, 255);
        }
}

void GenlistItemScenarioAction::sliderGreenSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        if (emission == "slider,changed")
        {
                double x;
                page->getDragValue("slider.green:slider", &x, NULL);

                action_temp.green = (int)(x * 100.0);

                evas_object_color_set(color_preview, action_temp.red, action_temp.green, action_temp.blue, 255);
        }
}

void GenlistItemScenarioAction::sliderBlueSignalCallback(void *data, Evas_Object *edje_object, string emission, string source)
{
        if (emission == "slider,changed")
        {
                double x;
                page->getDragValue("slider.blue:slider", &x, NULL);

                action_temp.blue = (int)(x * 100.0);

                evas_object_color_set(color_preview, action_temp.red, action_temp.green, action_temp.blue, 255);
        }
}
