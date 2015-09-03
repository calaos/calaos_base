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
#include "ExternProc.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define READBUFSIZE 65536

Eina_Bool ExternProcServer_con_add(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Con_Event_Client_Add *ev = reinterpret_cast<Ecore_Con_Event_Client_Add *>(event);
    ExternProcServer *ex = reinterpret_cast<ExternProcServer *>(data);

    if (!data || !ex ||
        ex->ipcServer != ecore_con_client_server_get(ev->client))
        return ECORE_CALLBACK_PASS_ON;

    ex->clientList.push_back(ev->client);

    return ECORE_CALLBACK_DONE;
}

Eina_Bool ExternProcServer_con_del(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Con_Event_Client_Del *ev = reinterpret_cast<Ecore_Con_Event_Client_Del *>(event);
    ExternProcServer *ex = reinterpret_cast<ExternProcServer *>(data);

    if (!data || !ex ||
        ex->ipcServer != ecore_con_client_server_get(ev->client))
        return ECORE_CALLBACK_PASS_ON;

    ex->clientList.remove(ev->client);

    return ECORE_CALLBACK_DONE;
}

Eina_Bool ExternProcServer_con_data(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Con_Event_Client_Data *ev = reinterpret_cast<Ecore_Con_Event_Client_Data *>(event);
    ExternProcServer *ex = reinterpret_cast<ExternProcServer *>(data);

    if (!data || !ex ||
        ex->ipcServer != ecore_con_client_server_get(ev->client))
        return ECORE_CALLBACK_PASS_ON;

    string d((char *)ev->data, ev->size);
    ex->processData(d);

    return ECORE_CALLBACK_DONE;
}

Eina_Bool ExternProcServer_con_error(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Con_Event_Client_Error *ev = reinterpret_cast<Ecore_Con_Event_Client_Error *>(event);
    ExternProcServer *ex = reinterpret_cast<ExternProcServer *>(data);

    if (!data || !ex ||
        ex->ipcServer != ecore_con_client_server_get(ev->client))
        return ECORE_CALLBACK_PASS_ON;

    cErrorDom("process") << "Error in local socket: " << ev->error;

    //remove client
    ex->clientList.remove(ev->client);

    return ECORE_CALLBACK_DONE;
}

Eina_Bool ExternProcServer_proc_del(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);
    ExternProcServer *ex = reinterpret_cast<ExternProcServer *>(data);

    if (!data || !ex ||
        ex->process_exe != ev->exe)
        return ECORE_CALLBACK_PASS_ON;

    cErrorDom("process") << "Process exited";

    ex->process_exe = nullptr; //ecore does free the Ecore_Exe object itself
    ex->processExited.emit();

    return ECORE_CALLBACK_DONE;
}

ExternProcServer::ExternProcServer(string pathprefix)
{
    int pid = getpid();
    sockpath = "/tmp/calaos_proc_";
    sockpath += pathprefix + "_" + Utils::to_string(pid);

    ipcServer = ecore_con_server_add(ECORE_CON_LOCAL_SYSTEM, sockpath.c_str(), 0, this);
    hAdd = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ADD,
                                   ExternProcServer_con_add,
                                   this);
    hData = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA,
                                    ExternProcServer_con_data,
                                    this);
    hDel = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DEL,
                                   ExternProcServer_con_del,
                                   this);
    hError = ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ERROR,
                                     ExternProcServer_con_error,
                                     this);
    hProcDel = ecore_event_handler_add(ECORE_EXE_EVENT_DEL,
                                       ExternProcServer_proc_del,
                                       this);
}

ExternProcServer::~ExternProcServer()
{
    ecore_event_handler_del(hAdd);
    ecore_event_handler_del(hData);
    ecore_event_handler_del(hDel);
    ecore_event_handler_del(hError);
    ecore_event_handler_del(hProcDel);
    ecore_exe_terminate(process_exe);
    ecore_exe_free(process_exe);

    ecore_file_unlink(sockpath.c_str());
}

void ExternProcServer::terminate()
{
    ecore_exe_terminate(process_exe);
}

void ExternProcServer::sendMessage(const string &data)
{
    for (Ecore_Con_Client *client : clientList)
    {
        ExternProcMessage msg(data);
        string frame = msg.getRawData();

        ecore_con_client_send(client, frame.c_str(), frame.size());
    }
}

void ExternProcServer::processData(const string &data)
{
    cDebugDom("process") << "Processing frame data " << data.size();

    recv_buffer += data;

    while (currentFrame.processFrameData(recv_buffer))
    {
        if (currentFrame.isValid())
        {
            cDebugDom("process") << "Got a new frame";

            messageReceived.emit(currentFrame.getPayload());

            currentFrame.clear();
        }
    }
}

void ExternProcServer::startProcess(const string &process, const string &name, const string &args)
{
    string cmd = process;
    cmd += " --socket \"" + sockpath + "|0\" --namespace \"" + name + "\" " + args;

    cDebugDom("process") << "Starting process: " << cmd;
    process_exe = ecore_exe_run(cmd.c_str(), this);
}

ExternProcMessage::ExternProcMessage()
{
    clear();
}

ExternProcMessage::ExternProcMessage(string data)
{
    payload = data;
    payload_length = data.size();
    isvalid = true;
    opcode = TypeMessage;
}

void ExternProcMessage::clear()
{
    payload.clear();
    payload_length = 0;
    isvalid = false;
    opcode = TypeUnkown;
    state = StateReadHeader;
}

bool ExternProcMessage::processFrameData(string &data)
{
    bool finished = false;

    while (!data.empty() && !finished)
    {
        switch (state)
        {
        case StateReadHeader:
        {
            if (data.size() >= 3)
            {
                //read header
                opcode = uint8_t(data[0]);
                //read length
                payload_length =
                        (uint8_t(data[1]) << 24) |
                        (uint8_t(data[2]) << 16) |
                        (uint8_t(data[3]) << 8) |
                        uint8_t(data[4]);

                data.erase(0, 5);

                if (opcode == TypeMessage)
                {
                    isvalid = true;
                    state = StateReadPayload;
                }
                else
                {
                    isvalid = false;
                    finished = false;
                    state = StateReadHeader;
                }
            }
            else
                return false;
            break;
        }
        case StateReadPayload:
        {
            if (!payload_length)
            {
                finished = true;
                state = StateReadHeader;
            }
            else
            {
                if (data.size() >= payload_length)
                {
                    payload = data.substr(0, payload_length);
                    data.erase(0, payload_length);

                    finished = true;
                    state = StateReadHeader;
                }
                else
                    return false;
            }
            break;
        }
        default:
            break;
        }
    }

    return finished;
}

string ExternProcMessage::getRawData()
{
    string frame;

    uint8_t b = static_cast<uint8_t>(opcode);
    frame.push_back(static_cast<char>(b));

    frame.push_back(static_cast<char>(payload_length >> 24));
    frame.push_back(static_cast<char>(payload_length >> 16));
    frame.push_back(static_cast<char>(payload_length >> 8));
    frame.push_back(static_cast<char>(payload_length));

    frame.append(payload);

    return frame;
}

ExternProcClient::ExternProcClient(int &argc, char **&argv)
{
    char *_sock = argvOptionParam(argv, argv + argc, "--socket");
    char *_name = argvOptionParam(argv, argv + argc, "--namespace");
    if (_sock)
    {
        sockpath = _sock;
        argc -= 2;
        argv += 2;
    }
    if (_name)
    {
        name = _name;
        argc -= 2;
        argv += 2;
    }
    else
        name = "extern_process";

    InitEinaLog(name.c_str());
}

ExternProcClient::~ExternProcClient()
{
    if (sockfd >= 0) close(sockfd);
}

bool ExternProcClient::connectSocket()
{
    if (!ecore_file_exists(sockpath.c_str()))
    {
        cError() << "Socket path " << sockpath << " not found";
        return false;
    }

    struct sockaddr_un remote;
    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        return false;
    }

    cDebug() << "Trying to connect to calaos_server...";

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, sockpath.c_str());
    int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
    if (connect(sockfd, (struct sockaddr *)&remote, len) == -1)
    {
        perror("connect");
        cError() << "Connect failed";
        return false;
    }

    return true;
}

bool ExternProcClient::processSocketRecv()
{
    char buff[READBUFSIZE];
    ssize_t len;

    len = recv(sockfd, buff, READBUFSIZE, 0);
    if (len <= 0)
    {
        cError() << "Error reading socket: " << strerror(errno);
        return false;
    }

    cDebugDom("process") << "Processing frame data " << len;
    recv_buffer.append(buff, buff + len);

    while (currentFrame.processFrameData(recv_buffer))
    {
        if (currentFrame.isValid())
        {
            cDebugDom("process") << "Got a new frame";

            messageReceived(currentFrame.getPayload());

            currentFrame.clear();
        }
    }

    return true;
}

void ExternProcClient::run(int timeoutms)
{
    bool quitloop = false;
    while (!quitloop)
    {
        fd_set events;
        struct timeval tv;

        FD_ZERO(&events);

        tv.tv_sec = timeoutms / 1000;
        tv.tv_usec = (timeoutms - tv.tv_sec * 1000) * 1000;

        FD_SET(sockfd, &events);

        for (int fd: userFds)
        {
            FD_SET(fd, &events);
        }

        if (!select(sockfd + 1, &events, NULL, NULL, &tv))
            readTimeout();

        if (FD_ISSET(sockfd, &events))
        {
            if (!processSocketRecv())
                quitloop = true;
        }

        if (!quitloop)
        {
            for (int fd: userFds)
            {
                if (FD_ISSET(fd, &events))
                {
                    if (!handleFdSet(fd))
                    {
                        quitloop = true;
                        break;
                    }
                }
            }
        }
    }
}

void ExternProcClient::sendMessage(const string &data)
{
    ExternProcMessage msg(data);
    string frame = msg.getRawData();
    ssize_t len;

    len = send(sockfd, frame.c_str(), frame.size(), 0);
    if (len < 0)
        cError() << "Error writing to socket: " << strerror(errno);
}

void ExternProcClient::appendFd(int fd)
{
    userFds.push_back(fd);
}

void ExternProcClient::removeFd(int fd)
{
    userFds.remove_if([=](const int &val)
    {
        return val == fd;
    });
}
