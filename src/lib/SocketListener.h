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
#ifndef  SOCKETLISTENER_INC
#define  SOCKETLISTENER_INC

#include <Ecore_Con.h>
#include <Utils.h>
#include <EcoreTimer.h>

namespace CalaosNetwork
{
        class SocketListener
        {
                private:
                        string address;
                        int port;
                        Ecore_Con_Server *econ;
                        bool login;

                        string buffer;

                        EcoreTimer *timer;

                        void processMessage(string msg);
                        void timerReconnect();

                public:
                        SocketListener(string address, int port);
                        ~SocketListener();


                        //Ecore Internal use only
                        void addConnection();
                        void delConnection();
                        void dataGet(void *data, int size);
        };
}

#endif

