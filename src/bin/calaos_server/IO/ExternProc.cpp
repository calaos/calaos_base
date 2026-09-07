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
#include <algorithm>
#include <limits>

#define READBUFSIZE 65536

/* A pipe hands over chunks, never lines, and a child that dumps binary or
 * dies mid-word never sends an end of line at all. Past this many bytes the
 * buffer is published and cut rather than kept growing, so no single line of
 * a sidecar can push the rest of a journal out of view.
 */
static const size_t kSidecarLineMax = 512;

ExternProcServer::ExternProcServer(string pathprefix)
{
    int pid = getpid();
    procName = pathprefix;
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

    //Only signal a process that really spawned: after a failed uv_spawn the
    //handle keeps pid 0, and uv_kill(0, SIGTERM) signals our whole process
    //group (this killed the make check harness, see KnxIo_test.cpp).
    if (process_exe && process_exe->referenced() && process_exe->pid() > 0)
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
    //A held relaunch armed before this call would spawn a child into a
    //controller that has just stopped wanting one.
    respawn_generation++;

    if (client)
        client->stop();

    //pid > 0 guard: a handle whose spawn failed keeps pid 0, and killing
    //pid 0 would SIGTERM our whole process group (see ~ExternProcServer)
    if (process_exe && process_exe->referenced() && process_exe->pid() > 0)
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

        /* Only what can never be a secret. The payload is whatever a driver
         * chose to put in it, and ReolinkCtrl hands the camera password over
         * as one of its fields; naming what IS published stays true when a
         * driver adds a credential, where a list of field names to withhold
         * goes silently wrong - the transport sees a string, not fields. The
         * sidecar, the frame type and the byte count keep a stalled or a
         * truncated exchange readable, which is what this line is read for.
         */
        cDebugDom("process") << "client writing data to " << procName
                             << ": opcode " << msg.getOpcode()
                             << ", " << data.size() << " bytes";

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
            //Same reason as the outgoing side, and ReolinkCtrl says it of
            //this very direction: it is the mirror of a channel that carries
            //credentials, so it refuses to log the message it receives.
            cDebugDom("process") << "Got a new frame from " << procName
                                 << ": opcode " << currentFrame.getOpcode()
                                 << ", " << currentFrame.getPayload().size()
                                 << " bytes";

            messageReceived.emit(currentFrame.getPayload());

            currentFrame.clear();
        }
    }

    if (currentFrame.hasError())
    {
        //framing violation: the stream cannot be trusted anymore
        cErrorDom("process") << "Framing error from external process, closing connection";
        currentFrame.clear();
        recv_buffer.clear();
        if (client)
            client->close();
    }
}

/* What a sidecar prints becomes an ordinary journal line.
 *
 * The only filter its output ever met was the CHILD's level; debug_level on
 * this side had no grip on it, and the line arrived with neither domain nor
 * level, indistinguishable from a bare write of the server itself.
 *
 * procName names the speaker, never the --namespace given to startProcess():
 * KNX runs two sidecars and hands both of them the same one.
 */
void ExternProcServer::relayChildOutput(string &buf, const char *stream, bool atEof)
{
    while (!buf.empty())
    {
        const size_t nl = buf.find('\n');
        string note;
        size_t take, drop;

        if (nl != string::npos)
        {
            take = std::min(nl, kSidecarLineMax);
            drop = nl + 1;
            if (nl > kSidecarLineMax)
                note = " [+" + Utils::to_string(nl - kSidecarLineMax) + " bytes cut]";
        }
        else if (buf.size() > kSidecarLineMax || atEof)
        {
            take = std::min(buf.size(), kSidecarLineMax);
            drop = take;
            note = " [no end of line]";
        }
        else
            return;

        cDebugDom("process") << procName << " " << stream << ": "
                             << buf.substr(0, take) << note;
        buf.erase(0, drop);
    }
}

double ExternProcServer::respawnDelay(int failures)
{
    if (failures <= 0)
        return 0.0;

    //Doubling by repeated multiplication rather than a power: the loop leaves
    //as soon as the ceiling is reached, so an absurd failure count costs a
    //handful of turns and can never overflow into infinity.
    double delay = kRespawnDelayMin;
    for (int i = 1; i < failures && delay < kRespawnDelayMax; i++)
        delay *= 2.0;

    return std::min(delay, kRespawnDelayMax);
}

int ExternProcServer::nextFailureCount(int failures, int64_t status, double ranSeconds)
{
    if (status == 0)
        return 0;

    if (failures < 0)
        failures = 0;

    if (ranSeconds >= kRespawnResetSeconds)
        return 1;

    //The delay has been at its ceiling for a very long time by then, and
    //wrapping would send it back to the bottom.
    if (failures == std::numeric_limits<int>::max())
        return failures;

    return failures + 1;
}

void ExternProcServer::noteChildGone(int64_t status)
{
    last_run_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - spawned_at).count();
    last_exit_status = status;
    respawn_failures = nextFailureCount(respawn_failures, status, last_run_seconds);
}

void ExternProcServer::startProcess(const string &process, const string &name, const vector<string> &args)
{
    const double hold = respawnDelay(respawn_failures);

    respawn_generation++;

    if (hold <= 0.0)
    {
        spawnProcess(process, name, args);
        return;
    }

    /* Both halves matter: with the cause alone, a relaunch that slows down
     * reads exactly like a sidecar that has gone quiet. */
    cWarningDom("process") << procName << " failed " << respawn_failures
                           << " time(s) in a row (last exit status "
                           << last_exit_status << "), holding the relaunch "
                           << hold << "s";

    const unsigned generation = respawn_generation;
    alive.singleShot(hold, [this, generation, process, name, args]()
    {
        if (generation != respawn_generation)
            return;
        spawnProcess(process, name, args);
    });
}

void ExternProcServer::spawnProcess(const string &process, const string &name, const vector<string> &args)
{
    isStarted = false;
    hasFailedStarting = false;

    vector<string> cmd = { process, "--socket", sockpath, "--namespace", name };
    cmd.insert(cmd.end(), args.begin(), args.end());

    process_exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    process_exe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &)
    {
        /* The status is the only thing a sidecar can say once it is gone. At
         * DEBUG it was invisible on a stock install, so a sidecar failing ten
         * times a second read as "process exited, restarting..." and nothing
         * else. A clean stop still says nothing: terminate() signals the
         * child, which leaves status 0. */
        noteChildGone(ev.status);
        if (ev.status != 0)
            cWarningDom("process") << procName << " exited with status " << ev.status
                                   << " after " << last_run_seconds << "s";
        else
            cDebugDom("process") << "ExternProcess exited: " << ev.status;
        process_exe->close();
        //T3.40: 100 ms with a raw `this`, and the server IS deleted inside
        //that window - ~RoonPlayer, ~KNXCtrl, ~WagoMap, ~OLACtrl, ~OWCtrl all
        //`delete process`, and LuaScript/ScriptExec.cpp:132 deletes it from
        //an idler armed by processExited itself. sigc::trackable does not
        //cover this: it is a lambda, not a mem_fun.
        alive.singleShot(0.1, [this]() { processExited.emit(); });
    });
    process_exe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        if (!isStarted) hasFailedStarting = true;
        //A spawn that never happened is a failure like any other: a missing
        //binary is the cheapest way there is to loop forever.
        noteChildGone(ev.code()? ev.code() : -1);
        cCriticalDom("process") << "Process error: " << ev.what();
        process_exe->close();
        //T3.40: same window as the ExitEvent above, and both can be armed in
        //the same run.
        alive.singleShot(0.1, [this]() { processExited.emit(); });
    });

    //Create a pipe for reading stdout
    pipe = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();
    pipe_stderr = uvw::Loop::getDefault()->resource<uvw::PipeHandle>();

    process_exe->stdio(static_cast<uvw::FileHandle>(0), uvw::ProcessHandle::StdIO::IGNORE_STREAM);

    // Configure stdout pipe
    uv_stdio_flags f = (uv_stdio_flags)(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    uvw::Flags<uvw::ProcessHandle::StdIO> ff(f);
    process_exe->stdio(*pipe, ff);

    // Configure stderr pipe
    process_exe->stdio(*pipe_stderr, ff);

    /* When pipes are closed, remove them and close them - after emptying what
     * they still hold. End of file is the only moment a child's last words can
     * be known to be complete, and a child that dies mid-word leaves them
     * without the end of line the loop above waits for: they are the ones that
     * say why it died.
     */
    pipe->once<uvw::EndEvent>([this](const auto &, auto &cl)
    {
        relayChildOutput(process_stdout, "stdout", true);
        cl.close();
    });
    pipe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &, auto &cl)
    {
        relayChildOutput(process_stdout, "stdout", true);
        cl.stop();
    });
    pipe_stderr->once<uvw::EndEvent>([this](const auto &, auto &cl)
    {
        relayChildOutput(process_stderr, "stderr", true);
        cl.close();
    });
    pipe_stderr->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &, auto &cl)
    {
        relayChildOutput(process_stderr, "stderr", true);
        cl.stop();
    });

    // Handler for stdout
    pipe->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        cDebugDom("process") << "Stdout data received: " << ev.length;
        process_stdout.append(string(ev.data.get(), ev.length));
        relayChildOutput(process_stdout, "stdout", false);
    });

    // Handler for stderr
    pipe_stderr->on<uvw::DataEvent>([this](uvw::DataEvent &ev, auto &)
    {
        cDebugDom("process") << "Stderr data received: " << ev.length;
        process_stderr.append(string(ev.data.get(), ev.length));
        relayChildOutput(process_stderr, "stderr", false);
    });

    auto parentEnvVar = [](const string &var)
    {
        char *env = getenv(var.c_str());
        if (env)
            return var + "=" + string(env);
        return var + "=";
    };

    std::vector<string> envVars = {
        "CALAOS_CACHE_PATH=" + Utils::getCachePath(),
        "CALAOS_CONFIG_PATH=" + Utils::getConfigPath(),
        "CALAOS_LOG_LEVEL=" + Utils::get_config_option("debug_level"),
        "CALAOS_LOG_DOMAINS=" + Utils::get_config_option("debug_domains"),
        "CALAOS_FORCE_COLOR=" + Logger::isColorEnabled(),
        parentEnvVar("PATH"),
        parentEnvVar("HOME"),
        parentEnvVar("LANG"),
        parentEnvVar("LC_ALL"),
        parentEnvVar("LANGUAGE"),
        parentEnvVar("LD_LIBRARY_PATH"),
        parentEnvVar("PWD"),
    };

    Utils::CStrArray env(envVars);
    Utils::CStrArray arr(cmd);      //vector form: nothing is re-split here

    /* Only what can never be a secret. An argument is any field a driver
     * chose to put there, and MqttWire hands the broker password over as one
     * of them; naming what IS published stays true when a driver adds a
     * credential, where a list of names to hide goes silently wrong. The
     * count keeps a cut or an extra argv visible, and the namespace plus one
     * line per launch keep a respawn loop readable.
     */
    cInfoDom("process") << "Starting process: " << cmd[0]
                        << " --socket " << sockpath
                        << " --namespace " << name
                        << " (" << args.size() << " argument(s))";

    spawned_at = std::chrono::steady_clock::now();
    process_exe->spawn(arr.at(0), arr.data(), env.data());

    if (!hasFailedStarting)
    {
        pipe->read();
        pipe_stderr->read();
    }

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
    has_error = false;
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
            //full header needed: 1 byte opcode + 4 bytes big endian length
            if (data.size() >= 5)
            {
                //read header
                opcode = uint8_t(data[0]);
                //read length, big endian
                payload_length =
                        (uint32_t(uint8_t(data[1])) << 24) |
                        (uint32_t(uint8_t(data[2])) << 16) |
                        (uint32_t(uint8_t(data[3])) << 8) |
                        uint32_t(uint8_t(data[4]));

                data.erase(0, 5);

                if (opcode != TypeMessage)
                {
                    isvalid = false;
                    finished = false;
                    state = StateReadHeader;
                }
                else if (payload_length > MaxPayloadLength)
                {
                    //An announced length above the cap means a broken or
                    //hostile peer. Drop the buffered data and flag the error
                    //so that callers close the connection instead of
                    //buffering an unbounded amount of memory.
                    cErrorDom("process") << "Framing error: announced payload length "
                                         << payload_length << " exceeds maximum "
                                         << MaxPayloadLength;
                    isvalid = false;
                    has_error = true;
                    payload_length = 0;
                    data.clear();
                    state = StateReadHeader;
                    return false;
                }
                else
                {
                    isvalid = true;
                    state = StateReadPayload;
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

    //reject a path that does not fit in sun_path (with its NUL terminator):
    //silently truncating would make us connect to a wrong path
    if (sockpath.length() >= sizeof(remote.sun_path))
    {
        cError() << "Socket path too long (" << sockpath.length()
                 << " bytes, max " << sizeof(remote.sun_path) - 1 << "): " << sockpath;
        return false;
    }

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        return false;
    }

    cDebug() << "Trying to connect to calaos_server...";

    remote.sun_family = AF_UNIX;
    strncpy(remote.sun_path, sockpath.c_str(), sizeof(remote.sun_path) - 1);
    remote.sun_path[sizeof(remote.sun_path) - 1] = '\0';
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

    if (currentFrame.hasError())
    {
        //framing violation: the stream cannot be trusted anymore
        cError() << "Framing error from server, closing connection";
        currentFrame.clear();
        recv_buffer.clear();
        return false;
    }

    return true;
}

bool ExternProcClient::run(int timeoutms)
{
    bool ok = true;
    loopQuit = false;
    while (!loopQuit)
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

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;

            /* NOT a normal end. EBADF here means a descriptor given by
             * appendFd() was closed by whoever owns it without being removed,
             * and the set has been polling a number that no longer belongs to
             * anyone - or worse, that a later open() has handed to something
             * else. Leaving quietly with a success status is what turns that
             * into a relaunch loop nobody can read. */
            cError() << "Main loop select() failed: " << strerror(errno);
            ok = false;
            break;
        }

        if (ret == 0)
            readTimeout();

        if (FD_ISSET(sockfd, &events))
        {
            if (!processSocketRecv())
                loopQuit = true;
        }

        if (!loopQuit)
        {
            /* A COPY, and it is not an optimisation to undo: handleFdSet() is
             * where a sidecar learns its descriptor is dead, so removeFd() from
             * inside it is the expected move - and erasing from the list being
             * walked leaves the iterator on a freed node. */
            const list<int> pollable = userFds;

            for (int fd: pollable)
            {
                if (FD_ISSET(fd, &events))
                {
                    if (!handleFdSet(fd))
                    {
                        loopQuit = true;
                        break;
                    }
                }
            }
        }
    }

    return ok;
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
