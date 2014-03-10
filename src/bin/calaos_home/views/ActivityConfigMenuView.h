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
#ifndef ACTIVITYCONFIGMENUVIEW_H
#define ACTIVITYCONFIGMENUVIEW_H

#include <Utils.h>

#include "ActivityView.h"

using namespace Utils;

class ActivityConfigMenuView: public ActivityView
{
private:
    Evas_Object *grid;
    Evas_Object *naviframe;

public:
    ActivityConfigMenuView(Evas *evas, Evas_Object *parent);
    ~ActivityConfigMenuView();

    virtual void resetView();

    virtual string getTitle() { return _("Configuration center"); }

    sigc::signal<void, string> menu_item_clicked;
};

#endif // ACTIVITYCONFIGMENUVIEW_H
