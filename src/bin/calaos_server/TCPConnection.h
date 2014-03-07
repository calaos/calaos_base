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
#ifndef S_TCPConnection_H
#define S_TCPConnection_H

#include <Calaos.h>
#include <Ecore_Con.h>
#include <WagoMap.h>
#include <ListeRoom.h>
#include <Room.h>
#include <AudioManager.h>
#include <AudioPlayer.h>
#include <CamManager.h>
#include <IPCam.h>
#include <InPlageHoraire.h>
#include <IPC.h>

using namespace Calaos;

typedef sigc::slot<void, Params &> ProcessDone_cb;
typedef sigc::signal<void, Params &> ProcessDone_signal;

class TCPConnectionData
{
public:
    ProcessDone_cb callback;

    int from;
    int to;
    string svalue;
    Params result;
};

class TCPConnection: public sigc::trackable
{
protected:

    Ecore_Con_Client *client_conn;

    bool login;
    string terminator, buffer;

    bool listen_mode = false;

    sigc::signal<void, string, string, void*, void*> sig_events;

    void ProcessRequest(Params &request, ProcessDone_cb callback);

    void BaseCommand(Params &request, ProcessDone_cb callback);
    void CameraCommand(Params &request, ProcessDone_cb callback);
    void HomeCommand(Params &request, ProcessDone_cb callback);
    void IOCommand(Params &request, ProcessDone_cb callback);
    void IRCommand(Params &request, ProcessDone_cb callback);
    void RulesCommand(Params &request, ProcessDone_cb callback);
    void AudioCommand(Params &request, ProcessDone_cb callback);
    void ScenarioCommand(Params &request, ProcessDone_cb callback);
    void ListenCommand();

    void CloseConnection();

    //IPC callback to handle all events from the system
    void HandleEventsFromSignals(string source, string emission, void *mydata, void *sender_data);

    //Callback when processing data is done and we want to send data back to the client
    void ProcessingDataDone(Params &request);

    /* Callbacks for async audio request */
    void get_volume_cb(AudioPlayerData data);
    void get_songinfo_cb(AudioPlayerData data);
    void get_album_cover_cb(AudioPlayerData data);
    void get_album_cover_id_cb(AudioPlayerData data);
    void get_current_time_cb(AudioPlayerData data);
    void get_status_cb(AudioPlayerData data);
    void get_playlist_size_cb(AudioPlayerData data);
    void get_playlist_current_cb(AudioPlayerData data);
    void get_playlist_item_cb(AudioPlayerData data);
    void get_playlist_cover_cb(AudioPlayerData data);
    void get_sync_status_cb(AudioPlayerData data);
    void get_sync_list_cb(AudioPlayerData data);
    void getdb_stats_cb(AudioPlayerData data);
    void getdb_years_cb(AudioPlayerData data);
    void getdb_default_cb(AudioPlayerData data);
    void getdb_default_param_cb(AudioPlayerData data);

public:
    TCPConnection(Ecore_Con_Client *cl);
    ~TCPConnection();

    /* Called by TCPServer whenever data comes in */
    void ProcessData(string data);
};

#endif
