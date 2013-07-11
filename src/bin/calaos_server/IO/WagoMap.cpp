/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <WagoMap.h>
#include <WagoCtrl.h>
#include <IPC.h>
#include <tcpsocket.h>

using namespace Utils;
using namespace Calaos;

static Eina_Bool _ecore_con_handler_data_get(void *data, int type, Ecore_Con_Event_Server_Data *ev);

WagoMapManager WagoMap::wagomaps;

WagoMap::WagoMap(std::string h, int p):
                host(h),
                port(p),
                quit_thread(false),
                mutex_queue(false),
                mutex_lock(false),
                udp_timer(NULL),
                udp_timeout_timer(NULL),
                econ(NULL)
{
        input_bits.resize(MBUS_MAX_BITS, false);
        output_bits.resize(MBUS_MAX_BITS, false);
        input_words.resize(MBUS_MAX_WORDS, 0);
        output_words.resize(MBUS_MAX_WORDS, 0);

        sigIPC.connect(sigc::mem_fun(*this, &WagoMap::IPCCallbacks));
        IPC::Instance().AddHandler("WagoMap", "*", sigIPC, NULL);

        event_handler_data_get = ecore_event_handler_add(ECORE_CON_EVENT_SERVER_DATA, (Ecore_Event_Handler_Cb)_ecore_con_handler_data_get, this);

        econ = ecore_con_server_connect(ECORE_CON_REMOTE_UDP,
                                        host.c_str(),
                                        WAGO_LISTEN_PORT,
                                        this);
        ecore_con_server_data_set(econ, this);

        heartbeat_timer = new EcoreTimer(0.1, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoHeartBeatTick));
        mbus_heartbeat_timer = new EcoreTimer(10.0, (sigc::slot<void>)sigc::mem_fun(*this, &WagoMap::WagoModbusHeartBeatTick));

        Start();

        Utils::logger("wago") << Priority::INFO << "WagoMap::WagoMap(" << host << "," << port << "): Ok" << log4cpp::eol;
}

WagoMap::~WagoMap()
{
        quit_thread = true;
        mutex_lock.condition_wake();

        ecore_event_handler_del(event_handler_data_get);
        ecore_con_server_del(econ);

        delete heartbeat_timer;
        delete mbus_heartbeat_timer;

        End();

        Utils::logger("wago") << Priority::INFO << "WagoMap::~WagoMap(): Ok" << log4cpp::eol;
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
                Utils::logger("wago") << Priority::ERROR << "WagoMap::WagoModbusReadHeartbeatCallback(): failed to read !" << log4cpp::eol;
}

void WagoMap::IPCCallbacks(string source, string emission, void *listener_data, void *sender_data)
{
        if (source != "WagoMap") return;

        WagoMapCmd *cmd = reinterpret_cast<WagoMapCmd *>(sender_data);
        if (!cmd) return;

        if (emission == "mbus,read,bits")
        {
                MultiBits_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiBits_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_bits);
        }
        else if (emission == "mbus,read,outbits")
        {
                MultiBits_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiBits_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_bits);
        }
        else if (emission == "mbus,write,bit")
        {
                SingleBit_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->singleBit_cb);
                sig.emit(cmd->status, cmd->address, cmd->value_bit);
        }
        else if (emission == "mbus,write,bits")
        {
                MultiBits_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiBits_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_bits);
        }
        if (emission == "mbus,read,words")
        {
                MultiWords_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiWords_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_words);
        }
        else if (emission == "mbus,read,outwords")
        {
                MultiWords_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiWords_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_words);
        }
        else if (emission == "mbus,write,word")
        {
                SingleBit_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->singleBit_cb);
                sig.emit(cmd->status, cmd->address, cmd->value_bit);
        }
        else if (emission == "mbus,write,words")
        {
                MultiWords_signal sig;
                if (cmd->mapSignals)
                        sig.connect(cmd->mapSignals->multiWords_cb);
                sig.emit(cmd->status, cmd->address, cmd->count, cmd->values_words);
        }

        cmd->deleteSignals();
}

void WagoMap::read_bits(UWord address, int nb, MultiBits_cb callback)
{
        WagoMapCmd cmd(MBUS_READ_BITS, address);
        cmd.createSignals();

        cmd.count = nb;
        cmd.mapSignals->multiBits_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::read_output_bits(UWord address, int nb, MultiBits_cb callback)
{
        WagoMapCmd cmd(MBUS_READ_OUTBITS, address);
        cmd.createSignals();

        cmd.count = nb;
        cmd.mapSignals->multiBits_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::write_single_bit(UWord address, bool val, SingleBit_cb callback)
{
        WagoMapCmd cmd(MBUS_WRITE_BIT, address);
        cmd.createSignals();

        cmd.value_bit = val;
        cmd.mapSignals->singleBit_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::write_multiple_bits(UWord address, int nb, vector<bool> &values, MultiBits_cb callback)
{
        WagoMapCmd cmd(MBUS_WRITE_BITS, address);
        cmd.createSignals();

        cmd.values_bits = values;
        cmd.count = nb;
        cmd.mapSignals->multiBits_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::read_words(UWord address, int nb, MultiWords_cb callback)
{
        WagoMapCmd cmd(MBUS_READ_WORDS, address);
        cmd.createSignals();

        cmd.count = nb;
        cmd.mapSignals->multiWords_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::read_output_words(UWord address, int nb, MultiWords_cb callback)
{
        WagoMapCmd cmd(MBUS_READ_OUTWORDS, address);
        cmd.createSignals();

        cmd.count = nb;
        cmd.mapSignals->multiWords_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::write_single_word(UWord address, UWord val, SingleWord_cb callback)
{
        WagoMapCmd cmd(MBUS_WRITE_WORD, address);
        cmd.createSignals();

        cmd.value_word = val;
        cmd.mapSignals->singleWord_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::write_multiple_words(UWord address, int nb, vector<UWord> &values, MultiWords_cb callback)
{
        WagoMapCmd cmd(MBUS_WRITE_WORDS, address);
        cmd.createSignals();

        cmd.values_words = values;
        cmd.count = nb;
        cmd.mapSignals->multiWords_cb = callback;

        queueAndSendCommand(cmd);
}

void WagoMap::queueAndSendCommand(WagoMapCmd cmd)
{
        mutex_queue.lock();
        mbus_commands.push(cmd);
        mutex_queue.unlock();

        mutex_lock.condition_wake();
}

void WagoMap::ThreadProc()
{
        WagoCtrl wago(host, port);

        while (true)
        {
                mutex_queue.lock();
                int queue_count = mbus_commands.size();
                mutex_queue.unlock();

                if (queue_count <= 0)
                        mutex_lock.condition_wait();

                if (quit_thread)
                        break;

                if (!wago.is_connected())
                {
                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, Connecting to " << host << log4cpp::eol;
                        wago.Connect();
                }

                mutex_queue.lock();
                if (mbus_commands.size() <= 0)
                {
                        mutex_queue.unlock();
                        continue;
                }
                WagoMapCmd cmd = mbus_commands.front();
                mbus_commands.pop();
                mutex_queue.unlock();

                switch(cmd.command)
                {
                case MBUS_READ_BITS:
                {
                        cmd.status = true;
                        if (!wago.read_bits(cmd.address, cmd.count, cmd.values_bits))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.read_bits(cmd.address, cmd.count, cmd.values_bits))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,read,bits", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_READ_OUTBITS:
                {
                        cmd.status = true;
                        if (!wago.read_bits(cmd.address + 0x200, cmd.count, cmd.values_bits))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.read_bits(cmd.address + 0x200, cmd.count, cmd.values_bits))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,read,outbits", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_WRITE_BIT:
                {
                        cmd.status = true;
                        if (!wago.write_single_bit(cmd.address, cmd.value_bit))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.write_single_bit(cmd.address, cmd.value_bit))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,write,bit", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_WRITE_BITS:
                {
                        cmd.status = true;
                        if (!wago.write_multiple_bits(cmd.address, cmd.count, cmd.values_bits))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.write_multiple_bits(cmd.address, cmd.count, cmd.values_bits))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,write,bits", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_READ_WORDS:
                {
                        cmd.status = true;
                        if (!wago.read_words(cmd.address, cmd.count, cmd.values_words))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.read_words(cmd.address, cmd.count, cmd.values_words))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,read,words", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_READ_OUTWORDS:
                {
                        cmd.status = true;
                        if (!wago.read_words(cmd.address + 0x200, cmd.count, cmd.values_words))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.read_words(cmd.address + 0x200, cmd.count, cmd.values_words))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,read,outwords", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_WRITE_WORD:
                {
                        cmd.status = true;
                        if (!wago.write_single_word(cmd.address, cmd.value_word))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.write_single_word(cmd.address, cmd.value_word))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,write,word", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                case MBUS_WRITE_WORDS:
                {
                        cmd.status = true;
                        if (!wago.write_multiple_words(cmd.address, cmd.count, cmd.values_words))
                        {
                                Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, reconnecting to " << host << log4cpp::eol;
                                wago.Connect();
                                if (!wago.write_multiple_words(cmd.address, cmd.count, cmd.values_words))
                                {
                                        cmd.status = false;
                                        Utils::logger("wago") << Priority::DEBUG << "WagoMap: MBUS, failed to send request" << log4cpp::eol;
                                }
                        }

                        IPC::Instance().SendEvent("WagoMap", "mbus,write,words", IPCData(new WagoMapCmd(cmd), new DeletorT<WagoMapCmd *>), true);
                }
                break;
                }
        }
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
                Utils::logger("wago") << Priority::CRIT
                                << "WagoMap:(): _ecore_con_handler_data_get, failed to get WagoMap object !"
                                << log4cpp::eol;
        }

        return ECORE_CALLBACK_RENEW;
}

void WagoMap::SendUDPCommand(string command, WagoUdp_cb callback)
{
        bool restart_timer = false;

        Utils::logger("wago") << Priority::DEBUG << "WagoMap: UDP, sending command: " << command << log4cpp::eol;

        if (udp_commands.empty())
                restart_timer = true;

        WagoMapCmd cmd(CALAOS_UDP_SEND, 0);
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

        Utils::logger("wago") << Priority::DEBUG << "WagoMap: UDP, sending command: " << command << log4cpp::eol;

        if (udp_commands.empty())
                restart_timer = true;

        WagoMapCmd cmd(CALAOS_UDP_SEND, 0);

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
        Utils::logger("wago") << Priority::DEBUG << "WagoMap: UDP, getting result for command " << cmd.udp_command << log4cpp::eol;

        cmd.udp_result = res;

        WagoUdp_signal sig;
        if (cmd.mapSignals)
                sig.connect(cmd.mapSignals->wagoUdp_cb);
        sig.emit(status, cmd.udp_command, cmd.udp_result);

        udp_commands.pop();
}

void WagoMap::UDPCommandTimeout_cb()
{
        Utils::logger("wago") << Priority::DEBUG << "WagoMap: UDP, Timeout ! " << log4cpp::eol;

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

        Utils::logger("wago") << Priority::DEBUG << "WagoMap: UDP, real sending command: " << cmd.udp_command << log4cpp::eol;

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

        bool found_ip = false;
        string ip;
        vector<string> intf = TCPSocket::getAllInterfaces();
        for (uint j = 0;j < intf.size() && !found_ip;j++)
        {
                ip = TCPSocket::GetLocalIP(intf[j]);

                if (ip == "") continue;
                vector<string> splitter, splitter2;
                Utils::split(ip, splitter, ".", 4);
                Utils::split(get_host(), splitter2, ".", 4);
                if (splitter[0] == splitter2[0] &&
                    splitter[1] == splitter2[1] &&
                    splitter[2] == splitter2[2])
                        found_ip = true;
        }

        if (found_ip)
        {
                string cmd = "WAGO_SET_SERVER_IP ";
                cmd += ip;

                SendUDPCommand(cmd);

                cmd = "WAGO_HEARTBEAT";
                SendUDPCommand(cmd);
        }
        else
        {
                Utils::logger("root") << Priority::DEBUG
                                << "WagoHeartbeat: No interface found corresponding to network : "
                                << get_host() << log4cpp::eol;
        }


}
