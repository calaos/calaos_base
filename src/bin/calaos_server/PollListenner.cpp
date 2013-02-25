/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <PollListenner.h>
#include <IPC.h>

//For UUID generation
#include <kashmir/uuid.h>
#include <kashmir/devrand.h>

using kashmir::uuid_t;
using kashmir::system::DevRand;

using namespace Calaos;

PollObject::PollObject(string _uuid):
        uuid(_uuid),
        timeout(NULL)
{
        timeout = new EcoreTimer(TIMEOUT_POLLLISTENNER, (sigc::slot<void>)sigc::mem_fun(*this, &PollObject::Timeout_cb));

        //Attach the callback to IPC
        sig_events.connect( sigc::mem_fun(*this, &PollObject::HandleEventsFromSignals) );
        IPC::Instance().AddHandler("events", "*", sig_events);

        Utils::logger("poll_listenner") << Priority::DEBUG << "PollObject: New object for " << uuid << log4cpp::eol;
}

PollObject::~PollObject()
{
        if (timeout)
        {
                delete timeout;
                timeout = NULL;
        }

        IPC::Instance().DeleteHandler(sig_events);

        Utils::logger("poll_listenner") << Priority::DEBUG << "~PollObject: Cleaning object " << uuid << log4cpp::eol;
}

void PollObject::HandleEventsFromSignals(string source, string emission, void *mydata, void *sender_data)
{
        vector<string> tokens;
        split(emission, tokens, " ");

        if (tokens.size() < 3)
                return;

        string id = tokens[0] + ":" + tokens[1];

        Utils::logger("poll_listenner") << Priority::DEBUG << "PollObject: Handling signal: " << id << " -> " << url_decode(tokens[2]) << log4cpp::eol;

        events.Add(id, url_decode(tokens[2]));
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

        Utils::logger("poll_listenner") << Priority::DEBUG << "PollObject: " << uuid << " Timeout !" << log4cpp::eol;

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
        DevRand devrandom;
        stringstream out;

        uuid_t _uuid;
        devrandom >> _uuid;
        out << _uuid;

        string uuid = out.str();

        pollobjects[uuid] = new PollObject(uuid);

        Utils::logger("poll_listenner") << Priority::DEBUG << "PollListenner::Register uuid:" << uuid << log4cpp::eol;

        return uuid;
}

bool PollListenner::Unregister(string uuid)
{
        if (pollobjects.find(uuid) == pollobjects.end())
        {
                Utils::logger("poll_listenner") << Priority::DEBUG << "PollListenner::Unregister uuid:" << uuid << " not found ! " << log4cpp::eol;
                return false;
        }

        PollObject *o = pollobjects[uuid];

        if (o)
        {
                delete o;
                pollobjects[uuid] = NULL;
        }

        pollobjects.erase(uuid);

        Utils::logger("poll_listenner") << Priority::DEBUG << "PollListenner::Unregister uuid:" << uuid << log4cpp::eol;

        return true;
}

bool PollListenner::GetEvents(string uuid, Params &events)
{
        if (pollobjects.find(uuid) == pollobjects.end())
        {
                Utils::logger("poll_listenner") << Priority::DEBUG << "PollListenner::GetEvents uuid:" << uuid << " not found ! " << log4cpp::eol;
                return false;
        }

        PollObject *o = pollobjects[uuid];

        events = o->getEvents();

        o->getEvents().clear();
        o->ResetTimer();

        return true;
}
