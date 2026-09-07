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
#ifndef EXTERNPROC_H
#define EXTERNPROC_H

#include "Utils.h"
#include "Calaos.h"
#include "Timer.h"

#include <chrono>

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class PipeHandle;
class ProcessHandle;
}

/*
 * Small framing for messages
 * +--------+----------------------+------------+
 * | OPCODE | LENGTH               | DATA ..... |
 * | 1 byte | 4 bytes (big endian) |            |
 * +--------+----------------------+------------+
 *
 * opcode: TypeMessage (0x21), any other value invalidates the frame
 * length: payload size in bytes, big endian, capped at MaxPayloadLength
 */

class ExternProcMessage
{
public:
    ExternProcMessage();
    ExternProcMessage(string data);

    //Maximum accepted payload length announced in a frame header. The peer is
    //a local spawned process, but a compromised/faulty one could announce up
    //to 4 GiB and make us buffer it all. 4 MiB is far above any legitimate
    //IPC message and aligned with the websocket transport frame cap.
    static constexpr uint32_t MaxPayloadLength = 4 * 1024 * 1024;

    bool isValid() const { return isvalid; }
    string getPayload() const { return payload; }

    //The frame type as it was read from the wire, so a journal can name the
    //kind of frame that was exchanged without naming its content.
    int getOpcode() const { return opcode; }

    //true when the framing was violated (oversized announced length).
    //The stream is not trustable anymore, callers should drop the connection.
    bool hasError() const { return has_error; }

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
    bool has_error = false;
};

class ExternProcServer: public sigc::trackable
{
    /* T3.40 - lifetime token for the deferred processExited.emit(). The
     * server is deleted from inside that very signal's handlers (see
     * ExternProc.cpp), and sigc::trackable does not disconnect a lambda. */
    LifetimeTag alive;

public:
    ExternProcServer(string pathprefix);
    ~ExternProcServer();

    void sendMessage(const string &data);

    /*
     * A VECTOR, AND NEVER A STRING TO RE-SPLIT.
     *
     * This used to take one string, concatenate it into a command line and
     * let Utils::CStrArray cut it back on the space - no quoting, no
     * escaping - so any configured value carrying a space became several
     * argv, the sidecar exited on its argument check and the 100 ms respawn
     * relaunched it forever. An MQTT password is the case with no other
     * answer: refusing a space there would refuse a legitimate configuration.
     * A caller that genuinely passes a LIST builds the vector itself.
     */
    void startProcess(const string &process, const string &name, const vector<string> &args = vector<string>());
    void terminate();

    /*
     * RELAUNCH THROTTLING, HERE AND NOT IN THE EIGHT CONTROLLERS THAT RESPAWN.
     *
     * processExited carries nothing, so every subscriber relaunched at the
     * same cadence whatever the sidecar answered: a broker that is switched
     * off cost a launch every ~110 ms, forever. The status is read on this
     * side already, so the ramp needs neither a new signature nor a decision
     * from any of them. What is held is the SPAWN and not the signal: a
     * controller must still learn AT ONCE that its sidecar is gone, or its
     * disconnect notice and its device lists go stale for as long as the hold.
     */
    static constexpr double kRespawnDelayMin = 0.1;
    static constexpr double kRespawnDelayMax = 30.0;

    /* A run at least this long is a new incident and not the same one
     * repeating, so it starts the ramp over - a sidecar that served for an
     * hour must not be picked up at the ceiling. Equal to the ceiling on
     * purpose: one number, and no window in which a child could die often
     * enough to escape the ramp yet still be looping. */
    static constexpr double kRespawnResetSeconds = kRespawnDelayMax;

    //Hold before the launch that follows `failures` consecutive failures.
    static double respawnDelay(int failures);

    //A zero status is a voluntary stop - terminate() signals the child, and a
    //SIGTERM leaves status 0 - so it never counts as a failure.
    static int nextFailureCount(int failures, int64_t status, double ranSeconds);

    int respawnFailures() const { return respawn_failures; }
    int64_t lastExitStatus() const { return last_exit_status; }
    double lastRunSeconds() const { return last_run_seconds; }

    sigc::signal<void, const string &> messageReceived;
    sigc::signal<void> processExited;
    sigc::signal<void> processConnected;

private:
    std::shared_ptr<uvw::PipeHandle> ipcServer, pipe, pipe_stderr;

    string sockpath;
    string procName;
    string recv_buffer;
    ExternProcMessage currentFrame;
    std::shared_ptr<uvw::ProcessHandle> process_exe;
    string process_stdout, process_stderr;

    std::shared_ptr<uvw::PipeHandle> client;

    bool isStarted = false;
    bool hasFailedStarting = false;

    int respawn_failures = 0;
    int64_t last_exit_status = 0;
    double last_run_seconds = 0.0;
    std::chrono::steady_clock::time_point spawned_at =
            std::chrono::steady_clock::now();

    //Bumped by anything that makes a pending held launch obsolete, so the
    //deferred callback can tell it is answering for a launch nobody wants
    //anymore. terminate() is the case that matters: without it a controller
    //that stops still gets a child up to half a minute later.
    unsigned respawn_generation = 0;

    void processData(const string &data);
    void relayChildOutput(string &buf, const char *stream, bool atEof);
    void spawnProcess(const string &process, const string &name, const vector<string> &args);
    void noteChildGone(int64_t status);
};

class ExternProcClient: public sigc::trackable
{
public:
    ExternProcClient(int &argc, char **&argv);
    virtual ~ExternProcClient();

    bool connectSocket();

    void sendMessage(const string &data);

    //setup if called first and if false is returned,
    //process quit
    virtual bool setup(int &argc, char **&argv) = 0;
    virtual int procMain() = 0; //the main function is this one

protected:
    virtual void readTimeout() = 0;
    virtual void messageReceived(const string &msg) = 0;

    //implement this when adding a file descriptor to the main loop
    //when something happens on this fd, this function is called.
    //return false to stop main loop, true otherwise
    virtual bool handleFdSet(int fd) { return true; }

    /* Minimal mainloop. Answers false when it ended on a failed select(),
     * which is never a normal end: a descriptor of the set was closed behind
     * the loop's back. Answering success there is what turns that into a
     * relaunch loop with nothing in any journal to explain it. */
    bool run(int timeoutms = 5000);

    //end run() from a callback that has no way to answer false
    void quitLoop() { loopQuit = true; }

    //append FD to be monitored by main loop
    void appendFd(int fd);
    void removeFd(int fd);

    //for external mainloop
    int getSocketFd() { return sockfd; }
    bool processSocketRecv(); //call if something needs to be read from socket

private:
    string sockpath;
    string name;
    //-1 so that a destruction before connectSocket() assigns it does not
    //close an arbitrary file descriptor
    int sockfd = -1;

    string recv_buffer;

    ExternProcMessage currentFrame;

    list<int> userFds;
    bool loopQuit = false;
};

#define EXTERN_PROC_CLIENT_CTOR(class_name) \
class_name(int &__argc, char **&__argv): ExternProcClient(__argc, __argv) {}

#define EXTERN_PROC_CLIENT_MAIN(class_name) \
int main(int argc, char **argv) \
{ \
    class_name inst(argc, argv); \
    if (inst.setup(argc, argv)) \
        return inst.procMain(); \
    return 1; \
}

#endif // EXTERNPROC_H
