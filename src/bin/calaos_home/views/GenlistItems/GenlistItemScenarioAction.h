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

#ifndef GenlistItemScenarioAction_H
#define GenlistItemScenarioAction_H

#include <Utils.h>
#include <GenlistItemBase.h>
#include <CalaosModel.h>
#include <ScenarioModel.h>

using namespace Utils;

class GenlistItemScenarioAction: public GenlistItemBase
{
private:
    ScenarioData &scenario_data;
    int sc_step;
    uint sc_action;

    IOActionList action;
    IOActionList action_temp;

    Evas_Object *popup;
    Evas_Object *pager_action;

    Evas_Object *color_preview;

    EdjeObject *page;

    Evas_Object *spin_hours;
    Evas_Object *spin_min;
    Evas_Object *spin_sec;
    Evas_Object *spin_ms;

    ScenarioAction &getAction();
    void createActionList(Evas_Object *glist, GenlistItemBase *header);

    void actionSimple(void *data, IOActionList ac);
    void actionSlider(void *data, IOActionList ac);
    void actionNumber(void *data, IOActionList ac);
    void actionText(void *data, IOActionList ac);
    void actionTime(void *data, IOActionList ac);
    void actionColor(void *data, IOActionList ac);

    void buttonBackClick(void *data, Evas_Object *edje_object, string emission, string source);
    void buttonValidClick(void *data, Evas_Object *edje_object, string emission, string source);
    void buttonValidTimeClick(void *data, Evas_Object *edje_object, string emission, string source);
    void sliderSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);
    void numberSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);
    void sliderRedSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);
    void sliderGreenSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);
    void sliderBlueSignalCallback(void *data, Evas_Object *edje_object, string emission, string source);

    void deleteItemSelected(void *data);

public:
    GenlistItemScenarioAction(Evas *evas, Evas_Object *parent, ScenarioData &scd, int step, int action, void *data = NULL);
    virtual ~GenlistItemScenarioAction();

    virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
    virtual string getLabelItem(Evas_Object *obj, string part);

    void buttonClickEdit();

    void setAction(IOActionList &ac) { action = ac; }

    sigc::signal<void, GenlistItemScenarioAction *, void *> delete_action;
};

#endif // GenlistItemScenarioAction_H
