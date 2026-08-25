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
#include <WagoMap.h>
#include <WagoCtrl.h>
#include "WagoWire.h"
#include <tcpsocket.h>
#include "Prefix.h"
#include "libuvw.h"

using namespace Utils;
using namespace Calaos;

WagoMapManager WagoMap::wagomaps;

WagoMap::WagoMap(std::string h, int p):
    host(h),
    port(p),
    udp_timer(NULL),
    udp_timeout_timer(NULL)
{
    input_bits.resize(MBUS_MAX_BITS, false);
    output_bits.resize(MBUS_MAX_BITS, false);
    input_words.resize(MBUS_MAX_WORDS, 0);
    output_words.resize(MBUS_MAX_WORDS, 0);

    createUdpSocket();

    heartbeat_timer = new Timer(0.1, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoHeartBeatTick));
    mbus_heartbeat_timer = new Timer(10.0, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoModbusHeartBeatTick));

    process = new ExternProcServer("wago");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_wago";

    process_args = host;
    process_args += " " + Utils::to_string(port);

    process->messageReceived.connect(sigc::mem_fun(*this, &WagoMap::processNewMessage));

    process->processExited.connect([=]()
    {
        onWagoDisconnected.emit();

        //restart process when stopped, with backoff so a driver failing
        //at startup does not respawn in a tight loop
        scheduleProcessRespawn();
    });

    process->startProcess(exe, "wago", process_args);

    process->processConnected.connect([=]()
    {
        //process is up and connected again: reset the respawn backoff
        respawn_attempts = 0;
        onWagoConnected.emit();
    });

    cInfoDom("wago") << host << "," << port;
}

WagoMap::~WagoMap()
{
    DELETE_NULL(respawn_timer);

    delete process;

    handleSrv->stop();
    handleSrv->close();

    delete heartbeat_timer;
    delete mbus_heartbeat_timer;

    //udp command timers are only created on demand, they were leaked before
    DELETE_NULL(udp_timer);
    DELETE_NULL(udp_timeout_timer);
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

void WagoMap::scheduleProcessRespawn()
{
    if (respawn_timer)
        return; //a respawn is already scheduled

    double delay = respawnDelay(respawn_attempts);
    respawn_attempts++;

    //never stop retrying (the Wago is the centerpiece of the installation),
    //but keep reminding loudly that something is wrong
    if (respawn_attempts % RESPAWN_LOG_EVERY == 0)
        cErrorDom("process") << "wago process still failing after "
                             << respawn_attempts << " attempts. Check " << exe
                             << " and the PLC at " << host << ":" << port;

    cWarningDom("process") << "process exited, restarting in " << delay
                           << "s (attempt " << respawn_attempts << ")";

    respawn_timer = new Timer(delay, [this]()
    {
        DELETE_NULL(respawn_timer);
        process->startProcess(exe, "wago", process_args);
    });
}

void WagoMap::stopAllWagoMaps()
{
    std::for_each(wagomaps.maps.begin(), wagomaps.maps.end(), Delete());
    wagomaps.maps.clear();
}

void WagoMap::createUdpSocket()
{
    auto loop = uvw::Loop::getDefault();
    handleSrv = loop->resource<uvw::UDPHandle>();

    handleSrv->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        string s(ev.data.get(), ev.length);
        this->udpRequest_cb(true, s);
    });

    handleSrv->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &)
    {
        cErrorDom("network") << "UDP server error: " << ev.what();
        udpProcessError();
    });

    handleSrv->bind(host, WAGO_LISTEN_PORT, uvw::UDPHandle::Bind::REUSEADDR);
    handleSrv->recv();
}

void WagoMap::WagoModbusHeartBeatTick()
{
    read_bits(WagoTypes::Address(0), WagoTypes::Count(1),
              sigc::mem_fun(*this, &WagoMap::WagoModbusReadHeartbeatCallback));
}

void WagoMap::WagoModbusReadHeartbeatCallback(bool status, WagoTypes::Address address, WagoTypes::Count count, vector<bool> &values)
{
    if (!status)
        cErrorDom("wago") << "failed to read !";
}

void WagoMap::processNewMessage(const string &msg)
{
    Params jsonData;
    vector<string> values;

    //E4.1h: ONE non-throwing parse for both the flattened Params and the
    //"values" array. The array used to be read straight off the raw parsed
    //root, separately from the flattening.
    if (!WagoWire::decodeMessage(msg, jsonData, &values))
    {
        cWarningDom("wago") << "Error parsing json from sub process";
        return;
    }

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
        for (const string &v: values)
            values_bits.push_back(v == "true");

        if (cmd.mapSignals)
            cmd.mapSignals->multiBits_cb(status, WagoTypes::Address(address),
                                          WagoTypes::Count(count), values_bits);
    }
    else if (cmd.command == MBUS_WRITE_BIT)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->singleBit_cb(status, WagoTypes::Address(address),
                                         WagoTypes::BitValue(false));
    }
    else if (cmd.command == MBUS_WRITE_BITS)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->multiBits_cb(status, WagoTypes::Address(address),
                                          WagoTypes::Count(count), values_bits);
    }
    else if (cmd.command == MBUS_READ_WORDS ||
             cmd.command == MBUS_READ_OUTWORDS)
    {
        for (const string &v: values)
        {
            UWord vv;
            Utils::from_string(v, vv);
            values_words.push_back(vv);
        }

        if (cmd.mapSignals)
            cmd.mapSignals->multiWords_cb(status, WagoTypes::Address(address),
                                          WagoTypes::Count(count), values_words);
    }
    else if (cmd.command == MBUS_WRITE_WORD)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->singleWord_cb(status, WagoTypes::Address(address),
                                          WagoTypes::WordValue(0));
    }
    else if (cmd.command == MBUS_WRITE_WORDS)
    {
        if (cmd.mapSignals)
            cmd.mapSignals->multiWords_cb(status, WagoTypes::Address(address),
                                          WagoTypes::Count(count), values_words);
    }

    cmd.deleteSignals();
}

void WagoMap::read_bits(WagoTypes::Address address, WagoTypes::Count nb, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_BITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildReadBitsRequest(cmd.wago_cmd_id, address, nb));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_output_bits(WagoTypes::Address address, WagoTypes::Count nb, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_OUTBITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildReadOutputBitsRequest(cmd.wago_cmd_id, address, nb));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_single_bit(WagoTypes::Address address, WagoTypes::BitValue val, SingleBit_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_BIT);
    cmd.createSignals();
    cmd.mapSignals->singleBit_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildWriteBitRequest(cmd.wago_cmd_id, address, val));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_multiple_bits(WagoTypes::Address address, WagoTypes::Count nb, vector<bool> &values, MultiBits_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_BITS);
    cmd.createSignals();
    cmd.mapSignals->multiBits_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    //E4.1h - this site used to build the "values" array into a separate object
    //it then threw away (leaking it) and send a second, fresh serialization of
    //the four-key Params instead, so the array never left the process. Both
    //the leak and the missing array are gone; the array is emitted by
    //WagoWire::buildWriteBitsRequest(), which explains why fixing it was safe.
    process->sendMessage(WagoWire::buildWriteBitsRequest(cmd.wago_cmd_id, address, nb, values));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_words(WagoTypes::Address address, WagoTypes::Count nb, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_WORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildReadWordsRequest(cmd.wago_cmd_id, address, nb));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::read_output_words(WagoTypes::Address address, WagoTypes::Count nb, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_READ_OUTWORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildReadOutputWordsRequest(cmd.wago_cmd_id, address, nb));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_single_word(WagoTypes::Address address, WagoTypes::WordValue val, SingleWord_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_WORD);
    cmd.createSignals();
    cmd.mapSignals->singleWord_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    process->sendMessage(WagoWire::buildWriteWordRequest(cmd.wago_cmd_id, address, val));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::write_multiple_words(WagoTypes::Address address, WagoTypes::Count nb, vector<UWord> &values, MultiWords_cb callback)
{
    WagoMapCmd cmd(MBUS_WRITE_WORDS);
    cmd.createSignals();
    cmd.mapSignals->multiWords_cb = callback;
    cmd.wago_cmd_id = Utils::createRandomUuid();

    //E4.1h - same defect as write_multiple_bits(), fixed the same way.
    //See WagoWire::buildWriteWordsRequest().
    process->sendMessage(WagoWire::buildWriteWordsRequest(cmd.wago_cmd_id, address, nb, values));

    mbus_commands[cmd.wago_cmd_id] = cmd;
}

void WagoMap::udpProcessError()
{
    //handleSrv->stop();
    //handleSrv->close();

    handleSrv->once<uvw::CloseEvent>([this](auto &, auto&)
    {
        this->createUdpSocket();
    });
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
        udp_timer = new Timer(50. / 1000., (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommand_cb));
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
        udp_timer = new Timer(50. / 1000., (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommand_cb));
    }
}

void WagoMap::udpRequest_cb(bool status, string res)
{
    if (udp_timeout_timer)
    {
        delete udp_timeout_timer;
        udp_timeout_timer = NULL;
    }

    if (udp_commands.empty())
        return;

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
        udp_timeout_timer = new Timer(2.0, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::UDPCommandTimeout_cb));

    handleSrv->send(host, WAGO_LISTEN_PORT, (char *)cmd.udp_command.c_str(), cmd.udp_command.length() + 1);

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
