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

/* T3.50 - the READ half of the reply path, typed per role.
 *
 * These two slots carry a read reply back from calaos_wago. They used to be
 * sigc::slot<void, bool, UWord, int, ...>: an address and a count side by
 * side, two types that convert into one another in BOTH directions with no
 * diagnostic at all. E4.1h measured the outbound twin of that pair GREEN on
 * a swap - 31/31, not one warning.
 *
 * ⚠️ Measured while typing them (T3.50.md section 7.2), and it corrects what
 * the fiche said: NOT ONE of the six implementations reads its `address` or
 * its `count` - they use `status` and `values` only, and the reply vector is
 * built from the JSON "values" array, never from `count`. So a permutation
 * here is today a SEMANTIC NO-OP, and what these types close is a CONTRACT
 * for the next implementation and the next emission site, not a live wrong
 * answer. Both values ARE live at the four emission sites, decoded from the
 * reply at WagoMap.cpp - that much of the fiche holds.
 *
 * The payloads are taken BY VALUE. The trailing vector is a non-const lvalue
 * reference and always was: sigc++ accepts it because processNewMessage()
 * hands over a NAMED LOCAL. The address and the count are passed as prvalues
 * once wrapped, so a `T &` on either of them does not build - measured,
 * mutation MXD-R. F-TYPE-5 cannot be re-armed silently on those two.
 */
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::Count, vector<bool> &> MultiBits_cb;
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::Count, vector<UWord> &> MultiWords_cb;

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
 * The two MULTI slots above are the READ half; T3.46 left them bare on scope
 * and T3.50 typed them, as T3.46.md section 7.6.1 required.
 */
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::BitValue> SingleBit_cb;
typedef sigc::slot<void, bool, WagoTypes::Address, WagoTypes::WordValue> SingleWord_cb;

/* T3.53 - the DALI UDP reply path, typed per role.
 *
 * These two carry a UDP reply from calaos_wago back to a DALI ballast. They
 * used to be sigc::slot/signal<void, bool, string, string>: the command and
 * the result of the reply, adjacent and of the SAME type.
 *
 * ⭐ That makes this the most exposed pair of the whole Wago chain and the
 * only one no compiler could ever have caught: MultiBits_cb and SingleBit_cb
 * above at least carried two types that convert into one another, so a
 * distinct spelling would have been diagnosable. Here the two spellings were
 * the same text - the swap mutation on master could not even be written.
 *
 * ⚠️ And both members are READ by every live implementation (measured, see
 * WagoTypes.h), so a permutation was never a no-op: WODali would search the
 * REPLY for "WAGO_DALI_GET" and never take its GET branch again, and each
 * WODaliRVB channel would store its own DALI address as its level.
 *
 * Taken BY VALUE, like every other role type of this header.
 */
typedef sigc::slot<void, bool, WagoTypes::UdpCommand, WagoTypes::UdpResult> WagoUdp_cb;
typedef sigc::signal<void, bool, WagoTypes::UdpCommand, WagoTypes::UdpResult> WagoUdp_signal;

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
    vector<string> process_args;

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

    /* WAGO_DALI_GET reads the group flag from its third parameter only from
     * the 3.0 PLC program on. Before 3.0, GET_PARAM_DINT has no "parameter
     * missing" guard, so a third field is not ignored: the older programs
     * read that position as the DMX read address, and a real 0 or 1 there
     * takes a DMX fixture out of its own branch. The flag is therefore
     * withheld until the PLC says which program it runs, and stays withheld
     * if the answer never comes: guessing 3.0 would turn a lost datagram
     * into a broken DMX read. */
    enum PlcVersionState { PLC_VERSION_PENDING, PLC_VERSION_KNOWN, PLC_VERSION_UNKNOWN };
    PlcVersionState plc_version_state = PLC_VERSION_PENDING;
    int plc_version_major = 0;
    int plc_version_minor = 0;

    struct DaliGetRequest
    {
        string line;
        string address;
        string group;
        WagoUdp_cb callback;
    };

    /* Reads handed over before the version was known. Every one of them must
     * leave: its owner counts itself into StartReadRules around this call,
     * and only the reply - or the 2s timeout - of the frame built from it
     * gives that count back. */
    vector<DaliGetRequest> pending_dali_gets;

    void plcVersionReply_cb(bool status, WagoTypes::UdpCommand command, WagoTypes::UdpResult result);
    void sendDaliGet(const DaliGetRequest &req);

    void createUdpSocket();

    void processNewMessage(const string &msg);

    /* Timer callback for udp commands */
    void UDPCommand_cb();
    void UDPCommandTimeout_cb();

    Timer *heartbeat_timer;

    void WagoHeartBeatTick();
    void WagoModbusHeartBeatTick();

    void WagoModbusReadHeartbeatCallback(bool status, WagoTypes::Address address, WagoTypes::Count count, vector<bool> &values);

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

    //First PLC program whose WAGO_DALI_GET reads a group flag in third
    //position, and whose GET_PARAM_DINT returns 0 for a parameter it does
    //not find. Both halves arrive together, in 3.0.
    static const int DALI_GROUP_MAJOR = 3;
    static const int DALI_GROUP_MINOR = 0;

    /* Queue one DALI state read. The group flag is appended only when this
     * PLC is known to be 3.0 or later; until the version query answers, the
     * request waits here rather than going out with a flag the older programs
     * would read as something else. */
    void SendDaliGetCommand(string line, string address, string group, WagoUdp_cb callback);

    bool plcDaliGetCarriesGroup() const;

    /* Private stuff used by C callbacks */
    void udpRequest_cb(bool status, string res);
    void udpProcessError();

    sigc::signal<void> onWagoConnected;
    sigc::signal<void> onWagoDisconnected;
};

}
#endif
