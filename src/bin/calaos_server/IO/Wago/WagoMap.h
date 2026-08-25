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
#ifndef S_WAGOMAP_H
#define S_WAGOMAP_H

#include <Calaos.h>
#include <Timer.h>
#include "ExternProc.h"
#include "WagoTypes.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class UDPHandle;
}

namespace Calaos
{

typedef sigc::slot<void, bool, UWord, int, vector<bool> &> MultiBits_cb;
typedef sigc::slot<void, bool, UWord, int, vector<UWord> &> MultiWords_cb;

/* T3.46 - the WRITE half of the reply path, typed per role.
 *
 * These two slots carry a write acknowledgement back from calaos_wago.
 * SingleWord_cb used to be sigc::slot<void, bool, UWord, UWord>: address and
 * value, two values of the same width side by side, interchangeable with no
 * diagnostic at all. That is the pair F-WAGO-7 names, arriving instead of
 * leaving, and it is the last hop of the round trip WagoTypes.h describes.
 * SingleBit_cb was <void, bool, UWord, bool> and carried TWO permutable pairs
 * in one signature: address/value across two mutually convertible widths, and
 * status/value which were both plain bool at positions 1 and 3.
 *
 * ⚠️ The two MULTI slots above are the READ half and are still bare - their
 * (UWord address, int count) is the pair E4.1h measured. Not typed here, and
 * that is a scope decision: see docs/refactoring/T3.46.md section 7.
 */
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::BitValue> SingleBit_cb;
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::WordValue> SingleWord_cb;

typedef sigc::slot<void, bool, string, string> WagoUdp_cb;
typedef sigc::signal<void, bool, string, string> WagoUdp_signal;

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

    string wago_cmd_id;

    bool no_callback;
    string udp_command;
    string udp_result;
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

    vector<WagoMap *> maps;
};

class WagoMap: public sigc::trackable
{

protected:
    std::string host;
    int port;

    ExternProcServer *process;
    string exe;
    string process_args;

    //T1.17: subprocess auto-restart backoff. Without it a driver failing at
    //startup respawns in a tight loop. Counter is reset when the process
    //connects successfully.
    Timer *respawn_timer = nullptr;
    int respawn_attempts = 0;

    void scheduleProcessRespawn();

    vector<bool> input_bits;
    vector<bool> output_bits;

    vector<UWord> input_words;
    vector<UWord> output_words;

    WagoMap(std::string host, int port);

    static WagoMapManager wagomaps;

    unordered_map<string, WagoMapCmd> mbus_commands;

    /* Heartbeat timer that do a modbus query to avoid TCP disconnection with the Wago */
    Timer *mbus_heartbeat_timer;

    queue<WagoMapCmd> udp_commands;
    Timer *udp_timer;
    Timer *udp_timeout_timer;
    std::shared_ptr<uvw::UDPHandle> handleSrv;

    void createUdpSocket();

    void processNewMessage(const string &msg);

    /* Timer callback for udp commands */
    void UDPCommand_cb();
    void UDPCommandTimeout_cb();

    Timer *heartbeat_timer;

    void WagoHeartBeatTick();
    void WagoModbusHeartBeatTick();

    void WagoModbusReadHeartbeatCallback(bool status, UWord address, int count, vector<bool> &values);

public:
    ~WagoMap();

    //Log a "still failing" error every N consecutive respawn attempts
    //(retries themselves never stop)
    static constexpr int RESPAWN_LOG_EVERY = 10;

    //Backoff delay in seconds before respawning the subprocess. The Wago is
    //the centerpiece of the installation, so we retry FOREVER: a short ramp
    //avoids a tight spawn loop, but the cap stays low (5s) so recovery is
    //fast once the PLC/network is back (e.g. after maintenance cut it).
    //attempt is the 0-based count of consecutive failures so far.
    static double respawnDelay(int attempt)
    {
        static const double delays[] = { 1.0, 2.0, 3.0, 5.0 };
        constexpr int ndelays = sizeof(delays) / sizeof(delays[0]);
        if (attempt < 0) attempt = 0;
        if (attempt >= ndelays) attempt = ndelays - 1;
        return delays[attempt];
    }

    //Singleton
    static WagoMap &Instance(std::string host, int port);
    static vector<WagoMap *> &get_maps() { return wagomaps.maps; }
    static void stopAllWagoMaps();

    //bits
    /* T3.31 - address, count and payload each have a type of their own, so a
     * caller can no longer hand them over in the wrong order. See
     * IO/Wago/WagoTypes.h for what that buys and what it leaves open. */
    void read_bits(WagoTypes::Address address, WagoTypes::Count nb, MultiBits_cb callback);
    void read_output_bits(WagoTypes::Address address, WagoTypes::Count nb, MultiBits_cb callback);
    void write_single_bit(WagoTypes::Address address, WagoTypes::BitValue val, SingleBit_cb callback);
    void write_multiple_bits(WagoTypes::Address address, WagoTypes::Count nb, vector<bool> &values, MultiBits_cb callback);

    //Words
    void read_words(WagoTypes::Address address, WagoTypes::Count nb, MultiWords_cb callback);
    void read_output_words(WagoTypes::Address address, WagoTypes::Count nb, MultiWords_cb callback);
    void write_single_word(WagoTypes::Address address, WagoTypes::WordValue val, SingleWord_cb callback);
    void write_multiple_words(WagoTypes::Address address, WagoTypes::Count nb, vector<UWord> &values, MultiWords_cb callback);

    std::string get_host() { return host; }
    int get_port() { return port; }

    //Send a command through the timer
    void SendUDPCommand(string cmd, WagoUdp_cb callback);
    void SendUDPCommand(string cmd);

    /* Private stuff used by C callbacks */
    void udpRequest_cb(bool status, string res);
    void udpProcessError();

    sigc::signal<void> onWagoConnected;
    sigc::signal<void> onWagoDisconnected;
};

}
#endif
