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

#ifndef GenlistItemScenarioScheduleTime_H
#define GenlistItemScenarioScheduleTime_H

#include <Utils.h>
#include <GenlistItemBase.h>

using namespace Utils;

class GenlistItemScenarioScheduleTime: public GenlistItemBase
{
    private:
        TimeRange range;

    public:
        GenlistItemScenarioScheduleTime(Evas *evas, Evas_Object *parent, TimeRange &range, void *data = NULL);
        virtual ~GenlistItemScenarioScheduleTime();

        virtual Evas_Object *getPartItem(Evas_Object *obj, string part);
        virtual string getLabelItem(Evas_Object *obj, string part);

        void buttonClickEdit();
        void buttonClickDelete();

        sigc::signal<void> edit_click;
        sigc::signal<void> del_click;
};

#endif // GenlistItemScenarioScheduleTime_H
