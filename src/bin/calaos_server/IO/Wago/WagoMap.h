/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_WAGOMAP_H
#define S_WAGOMAP_H

#include <Calaos.h>
#include <EcoreTimer.h>
#include <Ecore_Con.h>
#include "ExternProc.h"

namespace Calaos
{

typedef sigc::slot<void, bool, UWord, int, std::vector<bool> &> MultiBits_cb;
typedef sigc::slot<void, bool, UWord, bool> SingleBit_cb;
typedef sigc::slot<void, bool, UWord, int, std::vector<UWord> &> MultiWords_cb;
typedef sigc::slot<void, bool, UWord, UWord> SingleWord_cb;

typedef sigc::slot<void, bool, std::string, std::string> WagoUdp_cb;
typedef sigc::signal<void, bool, std::string, std::string> WagoUdp_signal;

enum { MBUS_NONE = 0, MBUS_READ_BITS, MBUS_READ_OUTBITS, MBUS_WRITE_BIT, MBUS_WRITE_BITS,
       MBUS_READ_WORDS, MBUS_READ_OUTWORDS, MBUS_WRITE_WORD, MBUS_WRITE_WORDS,
       CALAOS_UDP_SEND };

#define MBUS_MAX_BITS   512
#define MBUS_MAX_WORDS  512

class WagoMapSignals: public sigc::trackable
{
public:
    WagoMapSignals()
    { }

    MultiBits_cb multiBits_cb;
    SingleBit_cb singleBit_cb;
    MultiWords_cb multiWords_cb;
    SingleWord_cb singleWord_cb;

    WagoUdp_cb wagoUdp_cb;
};

class WagoMapCmd
{
public:
    WagoMapCmd(int _command = MBUS_NONE):
        command(_command),
        no_callback(false),
        inProgress(false),
        mapSignals(NULL)
    { }

    int command = MBUS_NONE;

    std::string wago_cmd_id;

    bool no_callback;
    std::string udp_command;
    std::string udp_result;
    bool inProgress;

    WagoMapSignals *mapSignals = nullptr;

    void createSignals() { if (!mapSignals) mapSignals = new WagoMapSignals(); }
    void deleteSignals() { DELETE_NULL(mapSignals); }
};

class WagoMap;
class WagoMapManager
{
public:
    ~WagoMapManager()
    {
        std::for_each(maps.begin(), maps.end(), Delete());
        maps.clear();
    }

    std::vector<WagoMap *> maps;
};

class WagoMap: public sigc::trackable
{

protected:
    std::string host;
    int port;

    ExternProcServer *process;
    std::string exe;

    std::vector<bool> input_bits;
    std::vector<bool> output_bits;

    std::vector<UWord> input_words;
    std::vector<UWord> output_words;

    WagoMap(std::string host, int port);

    static WagoMapManager wagomaps;

    std::unordered_map<std::string, WagoMapCmd> mbus_commands;

    /* Heartbeat timer that do a modbus query to avoid TCP disconnection with the Wago */
    EcoreTimer *mbus_heartbeat_timer;

    std::queue<WagoMapCmd> udp_commands;
    EcoreTimer *udp_timer;
    EcoreTimer *udp_timeout_timer;
    Ecore_Con_Server *econ;
    Ecore_Event_Handler *event_handler_data_get;

    void processNewMessage(const std::string &msg);

    /* Timer callback for udp commands */
    void UDPCommand_cb();
    void UDPCommandTimeout_cb();

    EcoreTimer *heartbeat_timer;

    void WagoHeartBeatTick();
    void WagoModbusHeartBeatTick();

    void WagoModbusReadHeartbeatCallback(bool status, UWord address, int count, std::vector<bool> &values);

public:
    ~WagoMap();

    //Singleton
    static WagoMap &Instance(std::string host, int port);
    static std::vector<WagoMap *> &get_maps() { return wagomaps.maps; }
    static void stopAllWagoMaps();

    //bits
    void read_bits(UWord address, int nb, MultiBits_cb callback);
    void read_output_bits(UWord address, int nb, MultiBits_cb callback);
    void write_single_bit(UWord address, bool val, SingleBit_cb callback);
    void write_multiple_bits(UWord address, int nb, std::vector<bool> &values, MultiBits_cb callback);

    //Words
    void read_words(UWord address, int nb, MultiWords_cb callback);
    void read_output_words(UWord address, int nb, MultiWords_cb callback);
    void write_single_word(UWord address, UWord val, SingleWord_cb callback);
    void write_multiple_words(UWord address, int nb, std::vector<UWord> &values, MultiWords_cb callback);

    std::string get_host() { return host; }
    int get_port() { return port; }

    //Send a command through the timer
    void SendUDPCommand(std::string cmd, WagoUdp_cb callback);
    void SendUDPCommand(std::string cmd);

    /* Private stuff used by C callbacks */
    void udpRequest_cb(bool status, std::string res);

    sigc::signal<void> onWagoConnected;
    sigc::signal<void> onWagoDisconnected;
};

}
#endif
