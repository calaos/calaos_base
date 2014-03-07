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
#ifndef ACTIVITYMEDIAVIEW_H
#define ACTIVITYMEDIAVIEW_H

#include <Utils.h>

#include "ActivityView.h"
#include "MainContentView.h"

using namespace Utils;

class ActivityMediaView: public ActivityView
{
private:
    MainContentView *contentView;

    void buttonPressed(void *data, Evas_Object *_edje, std::string emission, std::string source);

public:
    ActivityMediaView(Evas *evas, Evas_Object *parent);
    ~ActivityMediaView();

    virtual void resetView();

    void addView(BaseView *view) { contentView->addView(view); }
    BaseView *getTopView() { return contentView->getTopView(); }
    void removeTopView() { contentView->removeTopView(); }

    sigc::signal<void, string> button_clicked;
};

#endif // ACTIVITYMEDIAVIEW_H
