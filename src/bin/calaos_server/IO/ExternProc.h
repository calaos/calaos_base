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

#include "Utils.h"
#include "Calaos.h"
#include <jansson.h>
#include "Jansson_Addition.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class PipeHandle;
class ProcessHandle;
}

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

    void startProcess(const string &process, const string &name, const string &args = string());
    void terminate();

    sigc::signal<void, const string &> messageReceived;
    sigc::signal<void> processExited;
    sigc::signal<void> processConnected;

private:
    std::shared_ptr<uvw::PipeHandle> ipcServer;

    string sockpath;
    string recv_buffer;
    ExternProcMessage currentFrame;
    std::shared_ptr<uvw::ProcessHandle> process_exe;

    std::list<std::shared_ptr<uvw::PipeHandle>> clientList;

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

    //minimal mainloop
    void run(int timeoutms = 5000);

    //append FD to be monitored by main loop
    void appendFd(int fd);
    void removeFd(int fd);

    //for external mainloop
    int getSocketFd() { return sockfd; }
    bool processSocketRecv(); //call if something needs to be read from socket

private:
    string sockpath;
    string name;
    int sockfd;

    string recv_buffer;

    ExternProcMessage currentFrame;

    list<int> userFds;
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
