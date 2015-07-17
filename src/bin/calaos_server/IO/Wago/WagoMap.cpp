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
#include <WagoMap.h>
#include <WagoCtrl.h>
#include <tcpsocket.h>
#include "Prefix.h"

using namespace Utils;
using namespace Calaos;

static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Server_Data *ev);

WagoMapManager WagoMap::wagomaps;

WagoMap::WagoMap(std::string h, int p):
    host(h),
    port(p),
    udp_timer(NULL),
    udp_timeout_timer(NULL),
    econ(NULL)
{
    input_bits.resize(MBUS_MAX_BITS, false);
    output_bits.resize(MBUS_MAX_BITS, false);
    input_words.resize(MBUS_MAX_WORDS, 0);
    output_words.resize(MBUS_MAX_WORDS, 0);

    event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);

    econ = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                    host.c_str(),
                                    WAGO_LISTEN_PORT,
                                    this);
    ecore_con_server_data_set(econ, this);

    heartbeat_timer = new EcoreTimer(0.1, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoHeartBeatTick));
    mbus_heartbeat_timer = new EcoreTimer(10.0, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoModbusHeartBeatTick));

    process = new ExternProcServer("wago");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_wago";

    string args = host;
    args += " " + Utils::to_string(port);

    process->messageReceived.connect(sigc::mem_fun(*this, &WagoMap::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "wago", args);
    });

    process->startProcess(exe, "wago", args);

    cInfoDom("wago") << host << "," << port;
}

WagoMap::~WagoMap()
{
    delete process;

    ecore_event_handler_del(event_handler_data_get);
    ecore_con_server_del(econ);

    delete heartbeat_timer;
    delete mbus_heartbeat_timer;
}

WagoMap &WagoMap::Instance(std::string h, int p)
{
    for (uint i = 0;i < wagomaps.maps.size();i++)
    {
        if (wagomaps.maps[i]->get_host() == h &&
            wagomaps.maps[i]->get_port() == p)
        {
            return *wagomaps.maps[i];
        }
    }

    // Create a new wago mapping object
    WagoMap *mwago = new WagoMap(h, p);
    wagomaps.maps.push_back(mwago);

    return *wagomaps.maps[wagomaps.maps.size() - 1];
}

void WagoMap::stopAllWagoMaps()
{
    std::for_each(wagomaps.maps.begin(), wagomaps.maps.end(), Delete());
    wagomaps.maps.clear();
}

void WagoMap::WagoModbusHeartBeatTick()
{
    read_bits(0, 1, sigc::mem_fun(*this, &WagoMap::WagoModbusReadHeartbeatCallback));
}

void WagoMap::WagoModbusReadHeartbeatCallback(bool status, UWord address, int count, vector<bool> &values)
{
    if (!status)
        cErrorDom("wago") << "failed to read !";
}

void WagoMap::processNewMessage(const string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_object(jroot))
    {
        cWarningDom("1wire") << "Error parsing json from sub process: " << jerr.text;
        if (jroot)
            json_decref(jroot);
        return;
    }

    Params jsonData;
    jansson_decode_object(jroot, jsonData);

    if (mbus_commands.find(jsonData["id"]) == mbus_commands.end())
        return;

    WagoMapCmd cmd = mbus_commands[jsonData["id"]];
    mbus_commands.erase(jsonData["id"]);

    UWord address = 0;
    int count = 0;
    vector<bool> values_bits;
    vector<UWord> values_words;
    bool status = jsonData["status"] == "true";

    Utils::from_string(jsonData["address"], address);
    Utils::from_string(jsonData["count"], count);

    if (cmd.command == MBUS_READ_BITS ||
        cmd.command == MBUS_READ_OUTBITS)
    {
        uint idx;
        json_t *value;

        json_array_foreach(json_object_get(jroot, "values"), idx, value)
        {
            string v = json_string_value(value);
            values_bits.push_back(v == "true");
        }

        if (cmd.mapSignals)
            cmd.mapSignals->multiBits_cb(status, address, count, values_bits);
    }
    else if (cmd.command == MBUS_WRITE_BIT)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->singleBit_cb(status, address, false);
    }
    else if (cmd.command == MBUS_WRITE_BITS)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->multiBits_cb(status, address, count, values_bits);
    }
    else if (cmd.command == MBUS_READ_WORDS ||
             cmd.command == MBUS_READ_OUTWORDS)
    {
        uint idx;
        json_t *value;

        json_array_foreach(json_object_get(jroot, "values"), idx, value)
        {
            string v = json_string_value(value);
            UWord vv;
            Utils::from_string(v, vv);
            values_words.push_back(vv);
        }

        if (cmd.mapSignals)
            cmd.mapSignals->multiWords_cb(status, address, count, values_words);
    }
    else if (cmd.command == MBUS_WRITE_WORD)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->singleWord_cb(status, address, 0);
    }
    else if (cmd.command == MBUS_WRITE_WORDS)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->multiWords_cb(status, address, count, values_words);
    }

    cmd.deleteSignals();
}

void WagoMap::read_bits(UWord address, int nb, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_BITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "read_bits" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_output_bits(UWord address, int nb, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_OUTBITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "read_output_bits" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_single_bit(UWord address, bool val, SingleBit_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_BIT);
    cmd.createSignals();
    cmd.mapSignals->singleBit_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "write_bit" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "value", val?"true":"false" } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_multiple_bits(UWord address, int nb, vector<bool> &values, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_BITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "write_bits" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    json_t *jret = p.toJson();
    json_t *jarr = json_array();
    for (uint i = 0;i < values.size();i++)
        json_array_append_new(jarr, json_string(values[i]?"true":"false"));
    json_object_set_new(jret, "values", jarr);

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_words(UWord address, int nb, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_WORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "read_words" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_output_words(UWord address, int nb, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_OUTWORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "read_output_words" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_single_word(UWord address, UWord val, SingleWord_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_WORD);
    cmd.createSignals();
    cmd.mapSignals->singleWord_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "write_word" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "value", Utils::to_string(val) } };

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_multiple_words(UWord address, int nb, vector<UWord> &values, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_WORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    Params p = {{ "action", "write_words" },
                { "id", cmd.wago_cmd_id },
                { "address", Utils::to_string(address) },
                { "count", Utils::to_string(nb) } };

    json_t *jret = p.toJson();
    json_t *jarr = json_array();
    for (uint i = 0;i < values.size();i++)
        json_array_append_new(jarr, json_string(Utils::to_string(values[i]).c_str()));
    json_object_set_new(jret, "values", jarr);

    process->sendMessage(jansson_to_string(p.toJson()));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Server_Data *ev)
{
    WagoMap *w = reinterpret_cast<WagoMap *>(data);

    if (ev && ev->server && (w != ecore_con_server_data_get(ev->server)))
    {
        return ECORE_CALLBACK_PASS_ON;
    }

    if (w)
    {
        string d((char *)ev->data, ev->size);

        w->udpRequest_cb(true, d);
    }
    else
    {
        cCriticalDom("wago") << "failed to get WagoMap object !";
   }

    return ECORE_CALLBACK_RENEW;
}

void WagoMap::SendUDPCommand(string command, WagoUdp_cb callback)
{
    bool restart_timer = false;

    cDebugDom("wago") << "UDP, sending command: " << command;

    if (udp_commands.empty())
        restart_timer = true;

    WagoMapCmd cmd(CALAOS_UDP_SEND);
    cmd.createSignals();

    cmd.udp_command = command;
    cmd.mapSignals->wagoUdp_cb = callback;

    udp_commands.push(cmd);

    if (restart_timer)
    {
        if (udp_timer) delete udp_timer;
        udp_timer = new EcoreTimer(50. / 1000., (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommand_cb));
    }
}

void WagoMap::SendUDPCommand(string command)
{
    bool restart_timer = false;

    cDebugDom("wago") << "UDP, sending command: " << command;

    if (udp_commands.empty())
        restart_timer = true;

    WagoMapCmd cmd(CALAOS_UDP_SEND);

    cmd.no_callback = true;
    cmd.udp_command = command;

    udp_commands.push(cmd);

    if (restart_timer)
    {
        if (udp_timer) delete udp_timer;
        udp_timer = new EcoreTimer(50. / 1000., (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommand_cb));
    }
}

void WagoMap::udpRequest_cb(bool status, string res)
{
    if (udp_timeout_timer)
    {
        delete udp_timeout_timer;
        udp_timeout_timer = NULL;
    }

    WagoMapCmd &cmd = udp_commands.front();
    cDebugDom("wago") << "UDP, getting result for command " << cmd.udp_command;

    cmd.udp_result = res;

    WagoUdp_signal sig;
    if (cmd.mapSignals)
        sig.connect(cmd.mapSignals->wagoUdp_cb);
    sig.emit(status, cmd.udp_command, cmd.udp_result);

    udp_commands.pop();
}

void WagoMap::UDPCommandTimeout_cb()
{
    cDebugDom("wago") << "UDP, Timeout ! ";

    udpRequest_cb(false, "");
}

void WagoMap::UDPCommand_cb()
{
    if (!udp_commands.empty() && udp_commands.front().inProgress)
        return;

    if (udp_commands.empty())
    {
        delete udp_timer;
        udp_timer = NULL;

        return;
    }

    WagoMapCmd &cmd = udp_commands.front();

    cmd.inProgress = true;

    cDebugDom("wago") << "UDP, real sending command: " << cmd.udp_command;

    if (!udp_timeout_timer && !cmd.no_callback)
        udp_timeout_timer = new EcoreTimer(2.0, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommandTimeout_cb));

    ecore_con_server_send(econ, cmd.udp_command.c_str(), cmd.udp_command.length() + 1);

    if (cmd.no_callback)
        udp_commands.pop();
}

void WagoMap::WagoHeartBeatTick()
{
    if (heartbeat_timer->getTime() < 10.0)
        heartbeat_timer->Reset(10.0);

    string ip = TCPSocket::GetLocalIPFor(get_host());
    if (ip != "")
    {
        string cmd = "WAGO_SET_SERVER_IP ";
        cmd += ip;

        SendUDPCommand(cmd);

        cmd = "WAGO_HEARTBEAT";
        SendUDPCommand(cmd);
    }
    else
    {
        cDebugDom("wago") << "No interface found corresponding to network : " << get_host();
    }
}
