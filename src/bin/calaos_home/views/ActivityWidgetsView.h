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
#ifndef ACTIVITYWIDGETSVIEW_H
#define ACTIVITYWIDGETSVIEW_H

#include <Utils.h>
#include "ActivityView.h"
#include <Modules.h>
#include <Widget.h>

using namespace Utils;

class ActivityWidgetsView: public ActivityView
{
private:
    Evas_Object *clipper;

    vector<Widget *> widgets;

    //timer to do some maintenance stuff
    //like showing/hiding xmas widget
    EcoreTimer *timer;

    //                ModuleDef xmas_def;
    //                XmasWidget *xmas_widget;

    void _AddWidget(Widget *o);
    void TimerTick();

public:
    ActivityWidgetsView(Evas *evas, Evas_Object *parent);
    ~ActivityWidgetsView();

    virtual void resetView();

    void dimView();

    //Edje callbacks for widgets, so manager can delete them
    void Callback(void *data, Evas_Object *edje, std::string emission, std::string source);

    void EditMode();
    void NormalMode();
    void ResetPosition() { for (uint i = 0;i < widgets.size();i++) widgets[i]->Reset(); }
    void SaveWidgets();

    void LoadWidgets();

    bool AddWidget(ModuleDef &mtype, int x, int y, int w = 0, int h = 0, string id = "");
    void DeleteWidget(Widget *w);
    void DeleteAllWidgets();

    int size() { return widgets.size(); }
    Widget *getWidget(int i) { return widgets[i]; }
};

#endif // ACTIVITYWIDGETSVIEW_H
