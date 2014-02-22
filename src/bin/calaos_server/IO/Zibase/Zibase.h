/******************************************************************************
**  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_ZIBASE_H
#define S_ZIBASE_H

#include <Calaos.h>
#include <EcoreTimer.h>
#include <Ecore_Con.h>

#define ZIBASE_UDP_PORT     49999

class Zibase;
class ZibaseManager
{
        public:
                ~ZibaseManager()
                {
                        std::for_each(maps.begin(), maps.end(), Delete());
                        maps.clear();
                }

                vector<Zibase *> maps;
};

class ZibaseInfoSensor
{
        private:

        public:
                ZibaseInfoSensor() { }

                //TODO!
};

class Zibase
{

        protected:
                std::string host;
                int port;

                Zibase(std::string host, int port);

                static ZibaseManager zibasemaps;

                Ecore_Con_Server *econ_client, *econ_listen;
                Ecore_Event_Handler *event_handler_data_cl;
                Ecore_Event_Handler *event_handler_data_listen;

                friend Eina_Bool zibase_udpClientData(void *data, int type, Ecore_Con_Event_Client_Data *ev);
                friend Eina_Bool zibase_udpListenData(void *data, int type, Ecore_Con_Event_Server_Data *ev);

                void udpListenData(Ecore_Con_Event_Server_Data *ev);
                void udpClientData(Ecore_Con_Event_Client_Data *ev);

        public:
                ~Zibase();

                //Singleton
                static Zibase &Instance(std::string host, int port);
                static vector<Zibase *> &get_maps() { return zibasemaps.maps; }
                static void stopAllZibase();

                std::string get_host() { return host; }
                int get_port() { return port; }

                //IO classes needs to connect to this signal to receive sensor frames from zibase
                sigc::signal<void, ZibaseInfoSensor *> sig_newframe;
};

#endif
