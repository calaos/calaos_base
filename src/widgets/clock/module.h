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
#ifndef CALAOS_MODULE_H
#define CALAOS_MODULE_H

#include "CalaosModule.h"
#include "Utils.h"
#include "EdjeObject.h"
#include "Calendar.h"

class ModuleClock: public CalaosModuleBase
{
private:
    EdjeObject *edje_clock;
    Calendar *calendar;

public:
    ModuleClock(Evas *evas, string id, string module_path);

    virtual ~ModuleClock();

    virtual string getStringInfo();

    virtual void getSizeMin(int &w, int &h);
    virtual void getSizeMax(int &w, int &h);

    virtual Evas_Object *getEvasObject();
};

#endif
