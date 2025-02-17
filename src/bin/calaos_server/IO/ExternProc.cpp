/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include <sys/param.h>
#include "libuvw.h"
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
        std::shared_ptr<uvw::PipeHandle> cl = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
        ipcServer->accept(*cl);

        cDebugDom("process") << "New client connected to ExternProcServer";
        client = cl;
        processConnected.emit();

        //Setup events for client

        //When peer closed the connection, remove it from our map and close it
        client->on<uvw::EndEvent>([](const uvw::EndEvent &, auto &h)
        {
            cDebugDom("process") << "client EndEvent";
            h.close();
        });

        //When connection is closed
        client->on<uvw::CloseEvent>([this](const uvw::CloseEvent &, auto &)
        {
            cDebugDom("process") << "client closed, remove ref";
            client.reset();
        });

        client->on<uvw::DataEvent>([this](const uvw::DataEvent &ev, auto &)
        {
            cDebugDom("process") << "client DataEvent: " << ev.length;
            string d((char *)ev.data.get(), ev.length);
            this->processData(d);
        });

        client->once<uvw::ErrorEvent>([](const auto &, auto &h)
        {
            cDebugDom("process") << "Error sending data!";
            h.close();
        });

        client->read();
    });

    cDebugDom("process") << "New ExternProcServer listening to " << sockpath;
}

ExternProcServer::~ExternProcServer()
{
    ipcServer->stop();
    ipcServer->close();

    if (client)
    {
        cDebugDom("process") << "Stopping client";
        client->clear(); //Remove all connected slots, the ExternProcServer class will be deleted
        client->once<uvw::CloseEvent>([](const auto &, auto &) { cDebugDom("process") << "client closed."; });
        client->close();
    }

    if (process_exe && process_exe->referenced())
    {
        process_exe->kill(SIGTERM);
        process_exe->close();
    }

    if (pipe && pipe->referenced())
    {
        pipe->close();
    }

    cDebugDom("process") << "Deleting socket file: " << sockpath;
    FileUtils::unlink(sockpath);
}

void ExternProcServer::terminate()
{
    if (client)
        client->stop();

    if (process_exe && process_exe->referenced())
        process_exe->kill(SIGTERM);

    if (pipe && pipe->referenced())
    {
        pipe->close();
        pipe.reset();
    }
}

void ExternProcServer::sendMessage(const string &data)
{
    if (client)
    {
        ExternProcMessage msg(data);
        string frame = msg.getRawData();

        cDebugDom("process") << "client writing data: " << data;

        int dataSize = frame.length();
        auto dataWrite = std::unique_ptr<char[]>(new char[dataSize]);
        std::copy(frame.begin(), frame.end(), dataWrite.get());
        client->write(std::move(dataWrite), dataSize);
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
            cDebugDom("process") << "Got a new frame : " << currentFrame.getPayload();

            messageReceived.emit(currentFrame.getPayload());

            currentFrame.clear();
        }
    }
}

void ExternProcServer::startProcess(const string &process, const string &name, const string &args)
{
    isStarted = false;
    hasFailedStarting = false;
    string cmd = process + " --socket " + sockpath + " --namespace " + name + " " + args;

    process_exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    process_exe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &)
    {
        cDebugDom("process") << "ExternProcess exited: " << ev.status;
        process_exe->close();
        Timer::singleShot(0.1, [this]() { processExited.emit(); });
    });
    process_exe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        if (!isStarted) hasFailedStarting = true;
        cCriticalDom("process") << "Process error: " << ev.what();
        process_exe->close();
        Timer::singleShot(0.1, [this]() { processExited.emit(); });
    });

    //Create a pipe for reading stdout
    pipe = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
    process_exe->stdio(static_cast<uvw::FileHandle>(0), uvw::ProcessHandle::StdIO::IGNORE_STREAM);

    uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
    process_exe->stdio(*pipe, ff);

    //When pipe is closed, remove it and close it
    pipe->once<uvw::EndEvent>([](const uvw::EndEvent &, auto &cl) { cl.close(); });
    pipe->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &, auto &cl) { cl.stop(); });
    pipe->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        cDebugDom("process") << "Stdio data received: " << ev.length;
        process_stdout.append(string(ev.data.get(), ev.length));

        //Print lines which ends with endl only. keep remaining in buffer
        auto pos = process_stdout.find_first_of("\n");
        while (pos != std::string::npos)
        {
            std::cout << process_stdout.substr(0, pos) << std::endl;
            process_stdout.erase(0, pos + 1);
            pos = process_stdout.find_first_of("\n");
        }
    });

    Utils::CStrArray arr(cmd);
    cInfoDom("process") << "Starting process: " << arr.toString();
    process_exe->spawn(arr.at(0), arr.data());

    if (!hasFailedStarting)
        pipe->read();

    isStarted = true;
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
        int maxfd = -1;

        FD_ZERO(&events);

        tv.tv_sec = timeoutms / 1000;
        tv.tv_usec = (timeoutms - tv.tv_sec * 1000) * 1000;

        FD_SET(sockfd, &events);
        maxfd = sockfd;
        for (int fd: userFds)
        {
            FD_SET(fd, &events);
            maxfd = std::max(fd, maxfd);
        }

        int ret = select(maxfd + 1, &events, NULL, NULL, &tv);

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
