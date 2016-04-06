/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "ActivityController.h"

ActivityController::ActivityController(Evas *e, Evas_Object *p, int v):
    evas(e),
    parent(p),
    viewType(v),
    view(NULL)
{
}

ActivityController::~ActivityController()
{
    DELETE_NULL(view);
}

BaseView *ActivityController::getView()
{
    createView();

    return dynamic_cast<BaseView *>(view);
}

void ActivityController::createView()
{
    if (view) return;

    try
    {
        view = ActivityViewFactory::CreateView(evas, parent, viewType);
    }
    catch (std::exception const& e)
    {
        cCritical() <<  "Can't create view !";
        throw;
    }

    view->activity_quit.connect(sigc::mem_fun(wants_quit, &sigc::signal<void>::emit));
    view->view_deleted.connect(sigc::mem_fun(*this, &ActivityController::viewDeleted));

    view->controller = this;
}

void ActivityController::viewDeleted()
{
    view = NULL;
    view_deleted.emit(this);
    cDebug() <<  "view deleted";
}

void ActivityController::resetView()
{
    if (view)
        view->resetView();
}
