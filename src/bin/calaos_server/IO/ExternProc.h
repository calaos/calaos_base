#ifndef EXTERNPROC_H
#define EXTERNPROC_H

#include "Calaos.h"
#include <Ecore.h>
#include <Ecore_Con.h>

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

class ExternProcServer: sigc::trackable
{
public:
    ExternProcServer(string pathprefix);
    ~ExternProcServer();

    void sendMessage(const string &data);

    sigc::signal<void, const string &> messageReceived;

    void startProcess(const string &process);

    sigc::signal<void> processExited;

private:
    Ecore_Con_Server *ipcServer;
    Ecore_Event_Handler *hAdd, *hDel, *hData, *hError, *hProcDel;
    string sockpath;
    string recv_buffer;
    ExternProcMessage currentFrame;
    Ecore_Exe *process_exe;

    list<Ecore_Con_Client *> clientList;

    void processData(const string &data);

    friend Eina_Bool ExternProcServer_con_add(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_del(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_data(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_con_error(void *data, int type, void *event);
    friend Eina_Bool ExternProcServer_proc_del(void *data, int type, void *event);
};

#endif // EXTERNPROC_H
