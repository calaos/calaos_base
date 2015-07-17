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
#include <PollListenner.h>

using namespace Calaos;

PollObject::PollObject(string _uuid):
    uuid(_uuid),
    timeout(NULL)
{
    timeout = new EcoreTimer(TIMEOUT_POLLLISTENNER, (sigc::slot<void>)sigc::mem_fun(*this, &PollObject::Timeout_cb));

    EventManager::Instance().newEvent.connect(sigc::mem_fun(*this, &PollObject::handleEvents));

    cDebugDom("poll_listener") << "New object for " << uuid;
}

PollObject::~PollObject()
{
    if (timeout)
    {
        delete timeout;
        timeout = NULL;
    }

    cDebugDom("poll_listener") << "Cleaning object " << uuid;
}

void PollObject::handleEvents(const CalaosEvent &ev)
{
    events.push_back(ev);
}

Eina_Bool _timeout_poll_idler_cb(void *data)
{
    PollObject *obj = reinterpret_cast<PollObject *>(data);
    if (!obj) return ECORE_CALLBACK_CANCEL;

    PollListenner::Instance().Unregister(obj->getUUID());

    //delete the ecore_idler
    return ECORE_CALLBACK_CANCEL;
}

void PollObject::Timeout_cb()
{
    delete timeout;
    timeout = NULL;

    cDebugDom("poll_listener") << uuid << " Timeout !";

    ecore_idler_add(_timeout_poll_idler_cb, this);
}

PollListenner::PollListenner()
{
}

PollListenner::~PollListenner()
{
}

string PollListenner::Register()
{
    srand(time(NULL));
    stringstream ssUuid;
    ssUuid << std::hex << std::setfill('0') ;
    ssUuid << std::setw(4) << (rand() & 0xffff) << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << "-";
    ssUuid << std::setw(4) << (rand() & 0xffff) << std::setw(4) << (rand() & 0xffff)<< std::setw(4) << (rand() & 0xffff);

    string uuid = ssUuid.str();
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
