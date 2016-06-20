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
#ifndef ACTIVITYCONTROLLER_H
#define ACTIVITYCONTROLLER_H

#include <Utils.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Evas.h>
#include <Ecore_Evas.h>
#include <Edje.h>
#include <Elementary.h>

#include "ActivityView.h"


class ActivityController: public sigc::trackable
{
protected:
    Evas *evas;
    Evas_Object *parent;
    int viewType;

    ActivityView *view;

    virtual void createView();
    void viewDeleted();

public:
    ActivityController(Evas *evas, Evas_Object *parent, int viewType);
    virtual ~ActivityController();

    BaseView *getView();
    virtual void resetView();

    sigc::signal<void> wants_quit;
    sigc::signal<void, ActivityController *> view_deleted;

    virtual bool handleButtonClick(std::string button) { return false; }

};

#endif // ACTIVITYCONTROLLER_H
