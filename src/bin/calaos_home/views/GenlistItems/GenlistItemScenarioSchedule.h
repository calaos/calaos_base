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

#ifndef GenlistItemScenarioSchedule_H
#define GenlistItemScenarioSchedule_H

#include <Utils.h>
#include <GenlistItemBase.h>
#include "ScenarioModel.h"

using namespace Utils;

class GenlistItemSimple;
class GenlistItemScenarioSchedule: public GenlistItemBase, public IOBaseElement
{
private:
    Evas_Object *popup, *pager_popup;

    Scenario *scenario;

    void scenarioPlay(void *data);
    void scenarioModify(void *data);
    void scenarioDelete(void *data);
    void scheduleAdd(void *data);
    void scheduleModify(void *data);
    void scheduleDelete(void *data);

    virtual void ioDeleted();

    void deleteScenarioValid(void *data);
    void deleteScenarioCancel(void *data, GenlistItemSimple *cancel_item);

public:
    GenlistItemScenarioSchedule(Evas *evas, Evas_Object *parent, bool scheduleView, Scenario *scenario, void *data = NULL);
    virtual ~GenlistItemScenarioSchedule();

    virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
    virtual string getLabelItem(Evas_Object *obj, string part);

    void buttonClickMore();

    //Called when the real IO changed
    virtual void initView();
    virtual void updateView();

    sigc::signal<void, Scenario *> schedule_add_click;
    sigc::signal<void, Scenario *> schedule_modify_click;
    sigc::signal<void, Scenario *> schedule_del_click;
};

#endif // GenlistItemScenarioSchedule_H
