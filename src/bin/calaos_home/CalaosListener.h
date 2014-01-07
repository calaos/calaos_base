/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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

#ifndef CALAOSLISTENER_H
#define CALAOSLISTENER_H

#include <Ecore_Con.h>
#include <Utils.h>
#include <EcoreTimer.h>

class CalaosListener: public sigc::trackable
{
        public:
                //Ecore Internal use only
                void addConnection(Ecore_Con_Server *server);
                void delConnection(Ecore_Con_Server *server);
                void dataGet(Ecore_Con_Server *server, void *data, int size);
                void errorConnection(Ecore_Con_Server *server);

        private:
                string address;
                Ecore_Con_Server *econ;
                bool login;

                string buffer;

                Ecore_Event_Handler *handler_add;
                Ecore_Event_Handler *handler_data;
                Ecore_Event_Handler *handler_del;
                Ecore_Event_Handler *handler_error;

                void processMessage(string msg);

        public:
                CalaosListener(string address);
                ~CalaosListener();

                //RoomModel signals
                sigc::signal<void, string> notify_io_change;
                sigc::signal<void, string> notify_io_new;
                sigc::signal<void, string> notify_io_delete;
                sigc::signal<void, string> notify_room_change;
                sigc::signal<void, string> notify_room_new;
                sigc::signal<void, string> notify_room_delete;

                //AudioModel signals
                sigc::signal<void, string> notify_audio_change;

                //ScenarioModel signals
                sigc::signal<void, string> notify_scenario_add;
                sigc::signal<void, string> notify_scenario_del;
                sigc::signal<void, string> notify_scenario_change;

                sigc::signal<void> lost_connection;

};

#endif // CALAOSLISTENER_H
