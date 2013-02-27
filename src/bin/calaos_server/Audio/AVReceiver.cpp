/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <AVReceiver.h>
#include <IPC.h>
#include <AVRManager.h>

using namespace Calaos;

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
                Utils::logger("output") << Priority::CRIT
                                << "AVReceiver(): _con_server_add, failed to get object !"
                                << log4cpp::eol;

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
                Utils::logger("output") << Priority::CRIT
                                << "AVReceiver(): _con_server_del, failed to get object !"
                                << log4cpp::eol;

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
                Utils::logger("output") << Priority::CRIT
                                << "AVReceiver(): _con_server_data, failed to get object !"
                                << log4cpp::eol;

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
        Utils::logger("output") << Priority::DEBUG << "AVReceiver::AVReceiver(" << params["id"] << "): Ok" << log4cpp::eol;

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

        Utils::logger("output") << Priority::DEBUG << "AVReceiver::~AVReceiver(): Ok" << log4cpp::eol;
}

void AVReceiver::timerConnReconnect()
{
        Utils::logger("output") << Priority::DEBUG << "AVReceiver:timerConnReconnect() Connecting to " << host << ":" << port << log4cpp::eol;

        DELETE_NULL_FUNC(ecore_con_server_del, econ);
        econ = ecore_con_server_connect(ECORE_CON_REMOTE_TCP, host.c_str(), port, this);
        ecore_con_server_data_set(econ, this);

        Utils::logger("output") << Priority::DEBUG << "AVReceiver:timerConnReconnect(): econ == " << econ << log4cpp::eol;
}

void AVReceiver::addConnection(Ecore_Con_Server *srv)
{
        if (srv != econ) return;

        DELETE_NULL(timer_con);
        isConnected = true;

        connectionEstablished();

        Utils::logger("output") << Priority::DEBUG << "AVReceiver: main connection established" << log4cpp::eol;
}

void AVReceiver::delConnection(Ecore_Con_Server *srv)
{
        if (srv != econ) return;

        DELETE_NULL(timer_con);

        Utils::logger("output") << Priority::WARN << "AVReceiver: Main Connection closed !" << log4cpp::eol;
        Utils::logger("output") << Priority::WARN << "AVReceiver: Trying to reconnect..." << log4cpp::eol;

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

                Utils::logger("output") << Priority::DEBUG << "AVReceiver:getData() Bufferize data." << log4cpp::eol;

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

        Utils::logger("output") << Priority::DEBUG << "AVReceiver:getData() Got " << tokens.size() << " messages." << log4cpp::eol;

        for(uint i = 0; i < tokens.size(); i++)
                processMessage(tokens[i]);
}

void AVReceiver::processMessage(string msg)
{
        Utils::logger("output") << Priority::WARN << "AVReceiver:processMessage(): Should be inherited !" << log4cpp::eol;
}

void AVReceiver::processMessage(vector<char> msg)
{
        Utils::logger("output") << Priority::WARN << "AVReceiver:processMessage(): Should be inherited !" << log4cpp::eol;
}

void AVReceiver::sendRequest(string request)
{
        if (!econ || !isConnected) return;

        Utils::logger("output") << Priority::DEBUG << "AVReceiver::sendRequest() Command: " << request << log4cpp::eol;

        request += command_suffix;

        ecore_con_server_send(econ, request.c_str(), request.length());
}

void AVReceiver::sendRequest(vector<char> request)
{
        if (!econ || !isConnected) return;

        Utils::logger("output") << Priority::DEBUG << "AVReceiver::sendRequest(), " << request.size() << " bytes" << log4cpp::eol;

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
        Input(p),
        Output(p),
        zone(1)
{
        if (p.Exists("zone"))
                from_string(p["zone"], zone);
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
        EmitSignalInput();

        string sig = "input ";
        sig += Input::get_param("id") + " ";
        sig += url_encode(param + ":" + to_string(value));
        IPC::Instance().SendEvent("events", sig);

        sig = "output ";
        sig += Input::get_param("id") + " ";
        sig += url_encode(param + ":" + to_string(value));
        IPC::Instance().SendEvent("events", sig);
}

string IOAVReceiver::get_value_string()
{
        if (!receiver) return "";

        return to_string(receiver->getInputSource(zone));
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

        m["volume"] = to_string(receiver->getVolume(zone));
        m["input_source"] = to_string(receiver->getInputSource(zone));

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
        Utils::logger("output") << Priority::INFO << "IOAVReceiver(" << get_param("id") << "): got action, " << val << log4cpp::eol;

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
                Input::get_params().get_item(i, key, value);
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
                        m[to_string((*it).first)] = (*it).second;
        }

        return m;
}
