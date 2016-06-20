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
#ifndef PollListenner_H
#define PollListenner_H

#include "Calaos.h"
#include "EcoreTimer.h"
#include "EventManager.h"

#define TIMEOUT_POLLLISTENNER   300.0

namespace Calaos
{

class PollObject
{
private:
    sigc::signal<void, std::string, std::string, void*, void*> sig_events;

    std::string uuid;
    EcoreTimer *timeout; //timer that invalidates uuid after some time of inactivity

    //callback to handle all events from the system
    void handleEvents(const CalaosEvent &ev);

    //Timeout callback
    void Timeout_cb();

    std::list<CalaosEvent> events;

    sigc::connection evcon;

public:
    PollObject(std::string uuid);
    ~PollObject();

    std::list<CalaosEvent> &getEvents() { return events; }
    std::string getUUID() { return uuid; }
    void ResetTimer() { timeout->Reset(); }
    void clearEvents() { events.clear(); }
};

class PollListenner
{
private:
    std::map<std::string, PollObject *> pollobjects;

    PollListenner();

public:
    ~PollListenner();

    static PollListenner &Instance()
    {
        static PollListenner pl;

        return pl;
    }

    // Get an uuid.
    // The uuid will be automatically unregistered after some time
    // and become invalid
    std::string Register();

    // Unregister the uuid, return false if error
    bool Unregister(std::string uuid);

    // Get events for the registered uuid, return false if error
    bool GetEvents(std::string uuid, std::list<CalaosEvent> &events);
};

}
#endif
