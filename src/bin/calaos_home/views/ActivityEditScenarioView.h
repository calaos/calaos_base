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
#ifndef ActivityEditScenarioView_H
#define ActivityEditScenarioView_H

#include <Utils.h>

#include "ActivityView.h"
#include "GenlistItemBase.h"
#include "CalaosModel.h"


class GenlistItemSimple;
class GenlistItemScenarioAction;

class ActivityEditScenarioView: public ActivityView
{
private:
    Evas_Object *pager_step, *pager_step_popup, *popup_del;
    EdjeObject *pageName, *pageActions;

    int current_wizstep;

    Evas_Object *home_list, *actions_list;

    int current_step;

    ScenarioData scenario_data;

    std::list<IOBase *> cache_ios; //show all controlable ios

    Evas_Object *popup, *step_popup;

    Evas_Object *spin_hours;
    Evas_Object *spin_min;
    Evas_Object *spin_sec;
    Evas_Object *spin_ms;

    std::map<Room *, GenlistItemBase *> room_table;

    void buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void showStep(int step);
    void pageNameDeleted();
    void pageActionsDeleted();
    void loadPageActions();
    void loadActionsStep();
    std::string getIconForIO(IOBase *io);

    void pageNameEditName(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void pageNameEditName_cb(std::string text);

    void pageNameVisiblePressed(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void pageActionsCyclePressed(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void itemRoomSelected(void *data, Room *room);
    void updateVisibility();
    void updateCycling();

    void actionAddPressed(IOBase *io);

    void buttonStepPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void buttonStepAddPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);
    void buttonStepDelPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);

    void stepSelect(void *data, int step);
    void stepPauseChange(void *data, int step, GenlistItemSimple *item);
    void buttonPauseBackClick(void *data, Evas_Object *edje_object, std::string emission, std::string source);
    void buttonPauseValidTimeClick(void *data, Evas_Object *edje_object, std::string emission, std::string source, int step, GenlistItemSimple *item);

    void deleteStepValid(void *data);
    void deleteStepCancel(void *data);

    void actionDelete(GenlistItemScenarioAction *item, void *data, int it, Room *room);

public:
    ActivityEditScenarioView(Evas *evas, Evas_Object *parent);
    ~ActivityEditScenarioView();

    virtual void resetView();

    void setScenarioData(ScenarioData &data);
    ScenarioData &getScenarioData() { return scenario_data; }

    sigc::signal<void> buttonNextPressed;
    sigc::signal<void> buttonPreviousPressed;
    sigc::signal<void> buttonCancelPressed;
    sigc::signal<void> buttonValidPressed;
};

#endif // ActivityEditScenarioView_H
