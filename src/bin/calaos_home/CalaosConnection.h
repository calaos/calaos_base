/******************************************************************************
**  Copyright (c) 2006-2011, Calaos. All Rights Reserved.
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
#ifndef CALAOSCONNECTION_H
#define CALAOSCONNECTION_H

#include <Utils.h>

#include <Ecore.h>
#include <Ecore_Con.h>
#include <EcoreTimer.h>

#include "CalaosListener.h"

using namespace Utils;

#define TIMEOUT_CONNECT         60.0
#define TIMEOUT_SEND            60.0

typedef sigc::slot<void, bool, vector<string>, void * > CommandDone_cb;
typedef sigc::signal<void, bool, vector<string>, void * > CommandDone_sig;

class CalaosCmd
{
public:
    CalaosCmd():
        inProgress(false),
        noCallback(false)
    {}
    CalaosCmd(string c, CommandDone_cb cb):
        command(c),
        callback(cb),
        user_data(NULL),
        inProgress(false),
        noCallback(false)
    {}

    string command;
    CommandDone_cb callback;
    void *user_data;

    bool inProgress;
    bool noCallback;
};

class CalaosConnection: public sigc::trackable
{
public:
    //Ecore Internal use only
    void addConnection(Ecore_Con_Server *server);
    void delConnection(Ecore_Con_Server *server);
    void dataGet(Ecore_Con_Server *server, void *data, int size);

private:
    Ecore_Con_Server *econ;

    enum { CALAOS_CON_NONE, CALAOS_CON_LOGIN, CALAOS_CON_OK };

    int con_state;

    string host;

    EcoreTimer *timeout;

    Ecore_Event_Handler *event_handler_data_get;
    Ecore_Event_Handler *event_handler_add;
    Ecore_Event_Handler *event_handler_del;

    queue<CalaosCmd> commands;

    bool sendInProgress;

    CalaosListener *listener;

    void sendAndDequeue();
    void TimeoutTick();

public:
    CalaosConnection(string host, bool no_listenner = false);
    ~CalaosConnection();

    void SendCommand(string cmd, CommandDone_cb callback, void *data = NULL);
    void SendCommand(string cmd);

    CalaosListener *getListener() { return listener; }

    sigc::signal<void> error_login;
    sigc::signal<void> timeout_connect;
    sigc::signal<void> lost_connection;
    sigc::signal<void> connection_ok;
};

#endif // CALAOSCONNECTION_H
