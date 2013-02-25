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
#ifndef PollListenner_H
#define PollListenner_H

#include <Calaos.h>
#include <EcoreTimer.h>

#define TIMEOUT_POLLLISTENNER   300.0

namespace Calaos
{

class PollObject
{
        private:
                sigc::signal<void, string, string, void*, void*> sig_events;

                string uuid;
                EcoreTimer *timeout; //timer that invalidates uuid after some time of inactivity

                //IPC callback to handle all events from the system
                void HandleEventsFromSignals(string source, string emission, void *mydata, void *sender_data);

                //Timeout callback
                void Timeout_cb();

                Params events;

        public:
                PollObject(string uuid);
                ~PollObject();

                Params &getEvents() { return events; }
                string getUUID() { return uuid; }
                void ResetTimer() { timeout->Reset(); }
};

class PollListenner
{
        private:
                map<string, PollObject *> pollobjects;

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
                string Register();

                // Unregister the uuid, return false if error
                bool Unregister(string uuid);

                // Get events for the registered uuid, return false if error
                bool GetEvents(string uuid, Params &events);
};

}
#endif
