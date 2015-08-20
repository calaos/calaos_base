/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "AVReceiver.h"
#include "AVRManager.h"
#include "IOFactory.h"
#include "EventManager.h"

using namespace Calaos;

REGISTER_IO_USERTYPE(AVReceiver, IOAVReceiver)

#define AVR_TIMEOUT      40.0
#define AVR_RECONNECT    10.0

static Eina_Bool _con_server_add(void *data, int type, Ecore_Con_Event_Server_Add *ev)
{
    AVReceiver *o = reinterpret_cast<AVReceiver *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->addConnection(ev->server);
    else
        cCriticalDom("output") << "failed to get object !";
    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_del(void *data, int type, Ecore_Con_Event_Server_Del *ev)
{
    AVReceiver *o = reinterpret_cast<AVReceiver *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->delConnection(ev->server);
    else
        cCriticalDom("output") << "failed to get object !";

    return ECORE_CALLBACK_RENEW;
}

static Eina_Bool _con_server_data(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    AVReceiver *o = reinterpret_cast<AVReceiver *>(data);

    if (ev && ev->server && (o != ecore_con_server_data_get(ev->server)))
        return ECORE_CALLBACK_PASS_ON;

    if (o)
        o->dataGet(ev->server, ev->data, ev->size);
    else
        cCriticalDom("output") << "failed to get object !";

    return ECORE_CALLBACK_RENEW;
}

AVReceiver::AVReceiver(Params &p, int default_port, int _connection_type):
    ref_count(0),
    params(p),
    econ(NULL),
    timer_con(NULL),
    isConnected(false),
    volume_main(0),
    volume_zone2(0),
    volume_zone3(0),
    power_main(false),
    power_zone2(false),
    power_zone3(false),
    source_main(0),
    source_zone2(0),
    source_zone3(0),
    connection_type(_connection_type)
{
    cDebugDom("output") << params["id"];

    if (!params.Exists("visible")) params.Add("visible", "false");

    host = params["host"];
    port = default_port;

    if (params.Exists("port"))
        from_string(params["port"], port);

    ehandler_add = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_ADD, (Ecore_Event_Handler_Cb)_con_server_add, this);
    ehandler_del = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DEL, (Ecore_Event_Handler_Cb)_con_server_del, this);
    ehandler_data = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_con_server_data, this);

    timerConnReconnect();
    timer_con = new EcoreTimer(AVR_RECONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &AVReceiver::timerConnReconnect));
}

AVReceiver::~AVReceiver()
{
    DELETE_NULL(timer_con);
    DELETE_NULL_FUNC(ecore_con_server_del, econ);
    DELETE_NULL_FUNC(ecore_event_handler_del, ehandler_add);
    DELETE_NULL_FUNC(ecore_event_handler_del, ehandler_del);
    DELETE_NULL_FUNC(ecore_event_handler_del, ehandler_data);

    cDebugDom("output");
}

void AVReceiver::timerConnReconnect()
{
    cDebugDom("output") << "Connecting to " << host << ":" << port;

    DELETE_NULL_FUNC(ecore_con_server_del, econ);
    econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, host.c_str(), port, this);
    ecore_con_server_data_set(econ, this);

    cDebugDom("output") << "econ == " << econ;
}

void AVReceiver::addConnection(Ecore_Con_Server *srv)
{
    if (srv != econ) return;

    DELETE_NULL(timer_con);
    isConnected = true;

    connectionEstablished();

    cDebugDom("output") << "main connection established";
}

void AVReceiver::delConnection(Ecore_Con_Server *srv)
{
    if (srv != econ) return;

    DELETE_NULL(timer_con);

    cWarningDom("output") << "Main Connection closed !";
    cWarningDom("output") << "Trying to reconnect...";

    timer_con = new EcoreTimer(AVR_RECONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &AVReceiver::timerConnReconnect));

    isConnected = false;
}

void AVReceiver::dataGet(Ecore_Con_Server *srv, void *data, int size)
{
    if (srv != econ) return;

    if (connection_type == AVR_CON_CHAR)
    {
        string msg((char *)data, size);
        dataGet(msg);
    }
    else if (connection_type == AVR_CON_BYTES)
    {
        char *cdata = (char *)data;
        vector<char> d(cdata, cdata + size);

        //We don't know how to handle these messages,
        //so we delegate the processing to the child class
        //processMessage(vector<char>) has to be inherited !
        processMessage(d);
    }
}

void AVReceiver::dataGet(string msg)
{
    if (msg.find('\n') == string::npos &&
        msg.find('\r') == string::npos)
    {
        //We have not a complete paquet yet, buffurize it.
        recv_buffer += msg;

        cDebugDom("output") << "Bufferize data.";

        return;
    }

    if (!recv_buffer.empty())
    {
        //Put last data in buffer
        recv_buffer += msg;
        msg = recv_buffer;
        recv_buffer.clear();
    }

    replace_str(msg, "\r\n", "\n");
    replace_str(msg, "\r", "\n");

    vector<string> tokens;
    split(msg, tokens, "\n");

    cDebugDom("output") << "Got " << tokens.size() << " messages.";

    for(uint i = 0; i < tokens.size(); i++)
        processMessage(tokens[i]);
}

void AVReceiver::processMessage(string msg)
{
    cWarningDom("output") << "Should be inherited !";
}

void AVReceiver::processMessage(vector<char> msg)
{
    cWarningDom("output") << "Should be inherited !";
}

void AVReceiver::sendRequest(string request)
{
    if (!econ || !isConnected) return;

    cDebugDom("output") << "Command: " << request;

    request += command_suffix;

    ecore_con_server_send(econ, request.c_str(), request.length());
}

void AVReceiver::sendRequest(vector<char> request)
{
    if (!econ || !isConnected) return;

    cDebugDom("output") << request.size() << " bytes";

    ecore_con_server_send(econ, &request[0], request.size());
}

bool AVReceiver::getPower(int zone)
{
    if (zone == 2) return power_zone2;
    if (zone == 3) return power_zone3;
    return power_main;
}

int AVReceiver::getVolume(int zone)
{
    if (zone == 2) return volume_zone2;
    if (zone == 3) return volume_zone3;
    return volume_main;
}

int AVReceiver::getInputSource(int zone)
{
    if (zone == 2) return source_zone2;
    if (zone == 3) return source_zone3;
    return source_main;
}

IOAVReceiver::IOAVReceiver(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    zone(1)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("AVReceiver");
    ioDoc->descriptionSet(_("AVReceiver object to control network amplifier"));
    ioDoc->paramAdd("host", _("IP address of the device"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Port to use for connection"), 0, 65535, false);
    ioDoc->paramAddInt("zone", _("Zone of the amplifier (if supported)"), 0, 10, false);
    ioDoc->paramAdd("model", _("AVReceiver model. Supported: pioneer, denon, onkyo, marantz, yamaha"), IODoc::TYPE_STRING, true);

    ioDoc->actionAdd("power on", _("Switch receiver on"));
    ioDoc->actionAdd("power off", _("Switch receiver off"));
    ioDoc->actionAdd("volume 50", _("Set current volume"));
    ioDoc->actionAdd("source X", _("Change current input source"));
    ioDoc->actionAdd("custom XXXXXX", _("Send a custom command to receiver (if you know the protocol)"));

    get_params().Add("gui_type", "avreceiver");
    get_params().Add("visible", "false");
    if (get_params().Exists("zone"))
        from_string(get_param("zone"), zone);
    receiver = AVRManager::Instance().Create(p);

    if (zone == 1 && receiver)
        receiver->state_changed_1.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
    if (zone == 2 && receiver)
        receiver->state_changed_2.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
    if (zone == 3 && receiver)
        receiver->state_changed_3.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
}

IOAVReceiver::~IOAVReceiver()
{
    AVRManager::Instance().Delete(receiver);
}

void IOAVReceiver::statusChanged(string param, string value)
{
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { param, value } });
}

string IOAVReceiver::get_value_string()
{
    if (!receiver) return "";

    return Utils::to_string(receiver->getInputSource(zone));
}

map<string, string> IOAVReceiver::get_all_values_string()
{
    map<string, string> m;

    if (!receiver)
        return m;

    if (receiver->getPower(zone))
        m["power"] = "true";
    else
        m["power"] = "false";

    m["volume"] = Utils::to_string(receiver->getVolume(zone));
    m["input_source"] = Utils::to_string(receiver->getInputSource(zone));

    if (receiver->hasDisplay())
    {
        m["has_display"] = "true";
        m["display_text"] = receiver->getDisplayText();
    }
    else
        m["has_display"] = "false";

    return m;
}

void IOAVReceiver::force_input_string(string val)
{
    if (!isEnabled()) return;

    if (!receiver) return;

    if (val == "power off" || val == "power false")
        receiver->Power(false, zone);
    else if (val == "power on" || val == "power true")
        receiver->Power(true, zone);
    else if (val.substr(0, 7) == "volume ")
    {
        val.erase(0, 7);
        int vol;
        from_string(val, vol);
        receiver->setVolume(vol, zone);
    }
    else if (val.substr(0, 7) == "source ")
    {
        val.erase(0, 7);
        int source;
        from_string(val, source);
        receiver->selectInputSource(source, zone);
    }
    else if (val.substr(0, 7) == "custom ")
    {
        val.erase(0, 7);
        receiver->sendCustomCommand(val);
    }
}

bool IOAVReceiver::set_value(string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << get_param("id") << " got action, " << val;

    force_input_string(val);

    return true;
}

bool IOAVReceiver::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:avr");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

map<string, string> IOAVReceiver::query_param(string key)
{
    map<string, string> m;

    if (!receiver) return m;

    if (key == "input_sources")
    {
        AVRList sources = receiver->getSources();
        AVRList::iterator it = sources.begin();
        for (;it != sources.end();it++)
            m[Utils::to_string((*it).first)] = (*it).second;
    }

    return m;
}
