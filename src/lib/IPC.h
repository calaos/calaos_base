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
#ifndef CALAOS_IPC_h
#define CALAOS_IPC_h

#include <Utils.h>
#include <Mutex.h>
#include <sigc++/sigc++.h>
#include <Ecore.h>

using namespace Utils;

//maximum string length for an event name
#define MAX_EVENT_NAME          512

class IPCData
{
public:
    void *data;
    DeletorBase *destroy;   //Delete fonction that know how to free data

    IPCData(): data(NULL), destroy(NULL)
    { }
    IPCData(void *d, DeletorBase *del): data(d), destroy(del)
    { }
};

class IPCMsg
{
public:
    string source;
    string emission;
    void *data;
    bool auto_delete;       //If set, data will be auto deleted after use
    IPCData del_data;

    IPCMsg(): source("*"), emission("*"), data(NULL)
    { }
};

class IPCSignal
{
public:
    string source;
    string emission;
    void *data;
    sigc::signal<void, string, string, void*, void* > *signal;
};

/**
 * Do not remove an element from the list events outside the method IPC::BroadcastEvent() (see the method for more information)
 */
class IPC
{
private:
    int fd_read;
    int fd_write;

    Ecore_Fd_Handler *fd_handler;
    Mutex mutex;

    list<IPCMsg> events;
    list<IPCSignal> signals;

    //ctor
    IPC();

public:
    static IPC &Instance()
    {
        static IPC ipc;
        return ipc;
    }

    ~IPC();

    /**
                 * Add/delete a callback for event_name event.
                 * signal will be called when a events with the source and the emission is sent
                 * Parameters of the calback signal are:
                 *  - string: the source
                 *  - string: the emission
                 *  - void*: the data of the listener (you)
                 *  - void*: the data of the signal sender
                 */
    void AddHandler(string source, string emission,
                    sigc::signal<void, string, string, void*, void*> &signal,
                    void* data = NULL);
    void DeleteHandler(sigc::signal<void, string, string, void*, void*> &signal);

    //used by threads.
    void SendEvent(string source, string emission, void *data = NULL);

    //auto_delete_data flag is used to delete void *data automatically after use
    void SendEvent(string source, string emission, IPCData data, bool auto_delete_data = false);

    //called by _calaos_ipc_event()
    void BroadcastEvent();
};

#endif
