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
    ExternProcMessage(std::string data);

    bool isValid() const { return isvalid; }
    std::string getPayload() const { return payload; }

    void clear();

    bool processFrameData(std::string &data);
    std::string getRawData();

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
    std::string payload;
    bool isvalid;
};

class ExternProcServer: public sigc::trackable
{
public:
    ExternProcServer(std::string pathprefix);
    ~ExternProcServer();

    void sendMessage(const std::string &data);

    void startProcess(const std::string &process, const std::string &name, const std::string &args = std::string());
    void terminate();

    sigc::signal<void, const std::string &> messageReceived;
    sigc::signal<void> processExited;
    sigc::signal<void> processConnected;

private:
    Ecore_Con_Server *ipcServer;
    Ecore_Event_Handler *hAdd, *hDel, *hData, *hError, *hProcDel;
    std::string sockpath;
    std::string recv_buffer;
    ExternProcMessage currentFrame;
    Ecore_Exe *process_exe = nullptr;

    std::list<Ecore_Con_Client *> clientList;

    void processData(const std::string &data);

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

    void sendMessage(const std::string &data);

    //setup if called first and if false is returned,
    //process quit
    virtual bool setup(int &argc, char **&argv) = 0;
    virtual int procMain() = 0; //the main function is this one

protected:
    virtual void readTimeout() = 0;
    virtual void messageReceived(const std::string &msg) = 0;

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
    std::string sockpath;
    std::string name;
    int sockfd;

    std::string recv_buffer;

    ExternProcMessage currentFrame;

    std::list<int> userFds;
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
