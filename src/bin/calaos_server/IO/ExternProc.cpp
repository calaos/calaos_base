#include "ExternProc.h"

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

void ExternProcServer::startProcess(const string &process)
{
    string cmd = process;
    cmd += " " + sockpath;

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
                        (uint8_t(data[0]) << 24) |
                        (uint8_t(data[1]) << 16) |
                        (uint8_t(data[2]) << 8) |
                        uint8_t(data[3]);

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
