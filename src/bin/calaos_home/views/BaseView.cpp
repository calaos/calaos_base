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
#include "BaseView.h"

#include "ApplicationMain.h"

BaseView::BaseView(Evas *_e, Evas_Object *_p):
        EdjeObject(ApplicationMain::getTheme(), _e),
        parent(_p),
        controller(NULL)
{
        evas_object_data_set(edje, "BaseView", this);
}

BaseView::~BaseView()
{
        view_deleted.emit();
}

void BaseView::objectDeleted()
{
        cDebug() <<  "BaseView: Edje object deleted (" << collection  << ")";
}
