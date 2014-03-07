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
#ifndef BASEVIEW_H
#define BASEVIEW_H

#include <Utils.h>

#include <Eina.h>
#include <Ecore.h>
#include <Ecore_Con.h>
#include <Evas.h>
#include <Ecore_Evas.h>
#include <Edje.h>
#include <Elementary.h>

#include "EdjeObject.h"

#include <EcoreTimer.h>

using namespace Utils;

class ActivityController;
class BaseView: public EdjeObject
{
protected:
    Evas_Object *parent;

    virtual void objectDeleted();

public:
    BaseView(Evas *evas, Evas_Object *parent);
    virtual ~BaseView();

    virtual string getTitle() { return "None"; }

    sigc::signal<void> view_deleted;

    ActivityController *controller;
};

#endif // BASEVIEW_H
