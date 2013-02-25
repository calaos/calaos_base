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
#ifndef S_WAGOMAP_H
#define S_WAGOMAP_H

#include <Calaos.h>
#include <CThread.h>
#include <Mutex.h>
#include <mbus.h>
#include <EcoreTimer.h>
#include <Ecore_Con.h>

namespace Calaos
{

typedef sigc::slot<void, bool, UWord, int, vector<bool> &> MultiBits_cb;
typedef sigc::signal<void, bool, UWord, int, vector<bool> &> MultiBits_signal;

typedef sigc::slot<void, bool, UWord, bool> SingleBit_cb;
typedef sigc::signal<void, bool, UWord, bool> SingleBit_signal;

typedef sigc::slot<void, bool, UWord, int, vector<UWord> &> MultiWords_cb;
typedef sigc::signal<void, bool, UWord, int, vector<UWord> &> MultiWords_signal;

typedef sigc::slot<void, bool, UWord, UWord> SingleWord_cb;
typedef sigc::signal<void, bool, UWord, UWord> SingleWord_signal;

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
                WagoMapCmd(int _command, UWord _address):
                        command(_command),
                        address(_address),
                        no_callback(false),
                        inProgress(false),
                        mapSignals(NULL)
                { }

                int command;

                bool status;

                UWord address;
                int count;

                bool value_bit;
                vector<bool> values_bits;
                UWord value_word;
                vector<UWord> values_words;

                bool no_callback;
                string udp_command;
                string udp_result;
                bool inProgress;

                WagoMapSignals *mapSignals;

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

class WagoMap: public CThread
{

        protected:
                std::string host;
                int port;

                bool quit_thread;

                vector<bool> input_bits;
                vector<bool> output_bits;

                vector<UWord> input_words;
                vector<UWord> output_words;

                WagoMap(std::string host, int port);

                static WagoMapManager wagomaps;

                queue<WagoMapCmd> mbus_commands;
                Mutex mutex_queue;
                Mutex mutex_lock;
                sigc::signal<void, string, string, void *, void *> sigIPC;

                /* Heartbeat timer that do a modbus query to avoid TCP disconnection with the Wago */
                EcoreTimer *mbus_heartbeat_timer;

                queue<WagoMapCmd> udp_commands;
                EcoreTimer *udp_timer;
                EcoreTimer *udp_timeout_timer;
                Ecore_Con_Server *econ;
                Ecore_Event_Handler *event_handler_data_get;

                void queueAndSendCommand(WagoMapCmd cmd);

                /* Timer callback for udp commands */
                void UDPCommand_cb();
                void UDPCommandTimeout_cb();

                EcoreTimer *heartbeat_timer;

                void WagoHeartBeatTick();
                void WagoModbusHeartBeatTick();

                void WagoModbusReadHeartbeatCallback(bool status, UWord address, int count, vector<bool> &values);

        public:
                ~WagoMap();

                void IPCCallbacks(string source, string emission, void *listener_data, void *sender_data);

                //Singleton
                static WagoMap &Instance(std::string host, int port);
                static vector<WagoMap *> &get_maps() { return wagomaps.maps; }
                static void stopAllWagoMaps();

                //bits
                void read_bits(UWord address, int nb, MultiBits_cb callback);
                void read_output_bits(UWord address, int nb, MultiBits_cb callback);
                void write_single_bit(UWord address, bool val, SingleBit_cb callback);
                void write_multiple_bits(UWord address, int nb, vector<bool> &values, MultiBits_cb callback);

                //Words
                void read_words(UWord address, int nb, MultiWords_cb callback);
                void read_output_words(UWord address, int nb, MultiWords_cb callback);
                void write_single_word(UWord address, UWord val, SingleWord_cb callback);
                void write_multiple_words(UWord address, int nb, vector<UWord> &values, MultiWords_cb callback);

                std::string get_host() { return host; }
                int get_port() { return port; }

                //Send a command through the timer
                void SendUDPCommand(string cmd, WagoUdp_cb callback);
                void SendUDPCommand(string cmd);

                /* Private stuff used by C callbacks */
                void udpRequest_cb(bool status, string res);

                virtual void ThreadProc();
};

}
#endif
