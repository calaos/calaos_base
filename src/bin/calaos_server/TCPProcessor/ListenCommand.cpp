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
#include <TCPConnection.h>

using namespace CalaosNetwork;
using namespace Calaos;

void TCPConnection::HandleEventsFromSignals(string source, string emission, void *mydata, void *sender_data)
{
        if (source != "events") return;

        Utils::logger("root") << Priority::DEBUG << "TCPConnection::ListenCommand(): Sending event: " << emission << log4cpp::eol;

        emission += terminator;

        ecore_con_client_send(client_conn, emission.c_str(), emission.length());
}

void TCPConnection::ListenCommand()
{
        //Attach the callback to IPC
        sig_events.connect( sigc::mem_fun(*this, &TCPConnection::HandleEventsFromSignals) );
        IPC::Instance().AddHandler("events", "*", sig_events);
}
