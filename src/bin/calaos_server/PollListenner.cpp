/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "PollListenner.h"
#include "sole.hpp"

using namespace Calaos;

PollObject::PollObject(string _uuid):
    uuid(_uuid),
    timeout(NULL)
{
    timeout = new Timer(TIMEOUT_POLLLISTENNER, (sigc::slot<void>)sigc::mem_fun(*this, &PollObject::Timeout_cb));

    evcon = EventManager::Instance().newEvent.connect(sigc::mem_fun(*this, &PollObject::handleEvents));

    cDebugDom("poll_listener") << "New object for " << uuid;
}

PollObject::~PollObject()
{
    evcon.disconnect();

    if (timeout)
    {
        delete timeout;
        timeout = NULL;
    }
}

void PollObject::handleEvents(const CalaosEvent &ev)
{
    events.push_back(ev);
}

void PollObject::Timeout_cb()
{
    delete timeout;
    timeout = NULL;

    cDebugDom("poll_listener") << uuid << " Timeout !";

    Idler::singleIdler([=]()
    {
        PollListenner::Instance().Unregister(this->getUUID());
    });
}

PollListenner::PollListenner()
{
}

PollListenner::~PollListenner()
{
}

string PollListenner::Register()
{
    sole::uuid u4 = sole::uuid4();
    string uuid = u4.str();
    pollobjects[uuid] = new PollObject(uuid);

    cDebugDom("poll_listener") << "uuid:" << uuid;

    return uuid;
}

bool PollListenner::Unregister(string uuid)
{
    if (pollobjects.find(uuid) == pollobjects.end())
    {
        cDebugDom("poll_listener") << "uuid:" << uuid << " not found ! ";
        return false;
    }

    PollObject *o = pollobjects[uuid];

    if (o)
    {
        delete o;
        pollobjects[uuid] = NULL;
    }

    pollobjects.erase(uuid);

    cDebugDom("poll_listener") << "uuid:" << uuid;

    return true;
}

bool PollListenner::GetEvents(string uuid, list<CalaosEvent> &events)
{
    if (pollobjects.find(uuid) == pollobjects.end())
    {
        cDebugDom("poll_listener") << "uuid:" << uuid << " not found ! ";
        return false;
    }

    PollObject *o = pollobjects[uuid];

    events = o->getEvents();
    o->clearEvents();
    o->ResetTimer();

    return true;
}
