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
#include "uvw/src/uvw.hpp"
#include "Timer.h"

#define READBUFSIZE 65536

ExternProcServer::ExternProcServer(string pathprefix)
{
    int pid = getpid();
    sockpath = "/tmp/calaos_proc_";
    sockpath += Utils::createRandomUuid() + "_";
    sockpath += pathprefix + "_" + Utils::to_string(pid);

    ipcServer = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
    ipcServer->bind(sockpath);
    ipcServer->listen();

    ipcServer->on<uvw::ListenEvent>([this](const uvw::ListenEvent &, auto &)
    {
        //new client has just connected to us
        std::shared_ptr<uvw::PipeHandle> client = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        ipcServer->accept(*client);

        clientList.push_back(client);
        processConnected.emit();

        //Setup events for client

        //When peer closed the connection, remove it from our map and close it
        client->on<uvw::EndEvent>([client](const uvw::EndEvent &, auto &)
        {
            client->close();
        });

        //When connection is closed
        client->on<uvw::CloseEvent>([this, client](const uvw::CloseEvent &, auto &)
        {
            clientList.remove(client);
        });

        client->on<uvw::DataEvent>([this, client](const uvw::DataEvent &ev, auto &)
        {
            string d((char *)ev.data.get(), ev.length);
            this->processData(d);
        });

        client->read();
    });

    cDebugDom("process") << "New ExternProcServer listening to " << sockpath;
}

ExternProcServer::~ExternProcServer()
{
    ipcServer->stop();
    ipcServer->close();

    terminate();
    process_exe->close();

    cDebugDom("process") << "Deleting socket file: " << sockpath;
    auto fsReq = uvw::Loop::getDefault()->resource<uvw::FsReq>();
    fsReq->unlink(sockpath);
}

void ExternProcServer::terminate()
{
    if (process_exe->active())
        process_exe->kill(SIGTERM);
}

void ExternProcServer::sendMessage(const string &data)
{
    for (const auto &client: clientList)
    {
        ExternProcMessage msg(data);
        string frame = msg.getRawData();

        client->once<uvw::ErrorEvent>([this](const auto &, auto &)
        {
            cCriticalDom("process") << "Error sending data!";
        });

        client->write((char *)frame.c_str(), frame.size());
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
    string cmd = process + " --socket " + sockpath + " --namespace " + name + " " + args;

    cDebugDom("process") << "Starting process: " << process << " " << cmd;

    process_exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    process_exe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &, auto &)
    {
        process_exe->close();
        Timer::singleShot(0.1, [this]() { processExited.emit(); });
    });
    process_exe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        process_exe->close();
        Timer::singleShot(0.1, [this]() { processExited.emit(); });
    });

    Utils::CStrArray arr(cmd);
    process_exe->spawn(arr.at(0), arr.data());
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

    initLogger(name.c_str());
}

ExternProcClient::~ExternProcClient()
{
    if (sockfd >= 0) close(sockfd);
    Utils::freeLoggers();
}

bool ExternProcClient::connectSocket()
{
    if (!FileUtils::exists(sockpath))
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

        int ret = select(sockfd + 1, &events, NULL, NULL, &tv);

        if (ret == 0)
            readTimeout();
        else if (ret < 0)
            break;

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
