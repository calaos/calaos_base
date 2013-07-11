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
#ifndef S_CAMServer_H
#define S_CAMServer_H

#include <Calaos.h>
#include <CThread.h>
#include <CamConnection.h>
#include <tcpsocket.h>

namespace Calaos
{

class CamServer: public CThread
{
        protected:
                int port;
                TCPSocket *socket;
                vector<CamConnection *> connections;
                bool quit;

        public:
                CamServer(int port); //port to listen
                ~CamServer();

                virtual void ThreadProc(); //redefined

                void Clean();
};

}
#endif
