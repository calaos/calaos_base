/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#ifndef EXTERNPROC_H
#define EXTERNPROC_H

#include "Calaos.h"
#include <Ecore.h>
#include <Ecore_Con.h>
#include <jansson.h>
#include "Jansson_Addition.h"

/*
 * Small framing for messages
 * +-------+--------------+-------+------------+
 * | START | TYPE         | SIZE  | DATA ..... |
 * | 0x2   | 0x1 reserved | 2bytes|            |
 * +-------+--------------+-------+------------+
 *
 * type is not used for now
 * size of data is max: 2 bytes : 65536 bytes of data
 */

class ExternProcMessage
{
public:
    ExternProcMessage();
    ExternProcMessage(string data);

    bool isValid() const { return isvalid; }
    string getPayload() const { return payload; }

    void clear();

    bool processFrameData(string &data);
    string getRawData();

    enum TypeCode
    {
        TypeUnkown      = 0x00,
        TypeMessage     = 0x21,
    };

private:

    enum
    {
        StateReadHeader,
        StateReadPayload
    };
    int state;

    int opcode;
    uint32_t payload_length;
    string payload;
    bool isvalid;
};

class ExternProcServer: public sigc::trackable
{
public:
    ExternProcServer(string pathprefix);
    ~ExternProcServer();

    void sendMessage(const string &data);

    sigc::signal<void, const string &> messageReceived;

    void startProcess(const string &process, const string &name, const string &args);

    sigc::signal<void> processExited;

private:
    Ecore_Con_Server *ipcServer;
    Ecore_Event_Handler *hAdd, *hDel, *hData, *hError, *hProcDel;
    string sockpath;
    string recv_buffer;
    ExternProcMessage currentFrame;
    Ecore_Exe *process_exe = nullptr;

    list<Ecore_Con_Client *> clientList;

    void processData(const string &data);

    friend Eina_Bool ExternProcServer_con_add(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_del(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_data(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_error(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_proc_del(void *data, int type, void *event);
};

class ExternProcClient: public sigc::trackable
{
public:
    ExternProcClient(int &argc, char **&argv);
    ~ExternProcClient();

    bool connectSocket();

    void sendMessage(const string &data);
    sigc::signal<void, const string &> messageReceived;

    //for external mainloop
    int getSocketFd() { return sockfd; }
    bool processSocketRecv(); //call if something needs to be read from socket

    //minimal mainloop
    sigc::signal<void> readTimeout; //emited after read timeout, usefull for periodical work
    void run(int timeoutms = 5000);

private:
    string sockpath;
    string name;
    int sockfd;

    string recv_buffer;

    ExternProcMessage currentFrame;
};

#endif // EXTERNPROC_H
