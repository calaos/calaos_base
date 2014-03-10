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

#include "IOScenarioHomeView.h"

IOScenarioHomeView::IOScenarioHomeView(Evas *_evas, Evas_Object *_parent, IOBase *_io):
    IOView(_evas, _parent, _io, "calaos/element/scenario_home")
{
    addCallback("object", "go", sigc::mem_fun(*this, &IOScenarioHomeView::clickScenario));
}

IOScenarioHomeView::~IOScenarioHomeView()
{
}

void IOScenarioHomeView::initView()
{
    if (io)
    {
        EmitSignal("show,normal", "calaos");
    }
    else
    {
        EmitSignal("show,empty", "calaos");
        EmitSignal("scenario,false", "calaos");
    }

    updateView();
}

void IOScenarioHomeView::updateView()
{
    if (io)
    {
        setPartText("object.text", io->params["name"]);

        //Don't change button state if it's not a SimpleScenario
        if (io->params["ioBoolState"] == "")
            return;

        //Only send signal if state really changed.
        //Without that hack it breaks edje animation for state change
        if (io->params["state"] != state)
        {
            if (io->params["state"] == "true")
                EmitSignal("scenario,true", "calaos");
            else
                EmitSignal("scenario,false", "calaos");

            state = io->params["state"];
        }
    }
}

void IOScenarioHomeView::clickScenario(void *data, Evas_Object *edje_object, string emission, string source)
{
    if (!io) return;

    io->sendAction("true");

    if (io->params["ioBoolState"] == "")
    {
        EmitSignal("scenario,true", "calaos");

        //If it's not a SimpleScenario, just flash button when user click it.
        EcoreTimer::singleShot(0.7, sigc::mem_fun(*this, &IOScenarioHomeView::clickFlashButton_cb));
    }
}

void IOScenarioHomeView::clickFlashButton_cb()
{
    EmitSignal("scenario,false", "calaos");
}
