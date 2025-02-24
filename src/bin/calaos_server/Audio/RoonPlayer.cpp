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
#include "RoonPlayer.h"
#include "IOFactory.h"
#include <memory>
#include "Prefix.h"
#include "Utils.h"

using namespace Calaos;

REGISTER_IO_USERTYPE(roon, RoonPlayer)

RoonCtrl::RoonCtrl(const string &host, int port)
{
    cDebugDom("roon") << "new RoonCtrl: " << host;
    process = new ExternProcServer("roon");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_roon";

    process->messageReceived.connect(sigc::mem_fun(*this, &RoonCtrl::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("roon") << "process exited, restarting...";
        process->startProcess(exe, "roon");
    });

    string args;
    if (!host.empty())
        args += " --host " + host + " --port " + Utils::to_string(port);

    process->startProcess(exe, "roon", args);
}

RoonCtrl::~RoonCtrl()
{
    delete process;
}

shared_ptr<RoonCtrl> RoonCtrl::Instance(const string &host, int port)
{
    static shared_ptr<RoonCtrl> instance(new RoonCtrl(host, port));
    return instance;
}

/*
Typical message from Roon:
{
  "zone_id": "160132a7337c26b01e556c2809514e65d6a0",
  "display_name": "RoseSalon",
  "outputs": [
    {
      "output_id": "170132a7337c26b01e556c2809514e65d6a0",
      "zone_id": "160132a7337c26b01e556c2809514e65d6a0",
      "can_group_with_output_ids": [
        "170132a7337c26b01e556c2809514e65d6a0"
      ],
      "display_name": "RoseSalon",
      "volume": {
        "type": "number",
        "min": 0,
        "max": 99,
        "value": 24,
        "step": 1,
        "is_muted": false,
        "hard_limit_min": 0,
        "hard_limit_max": 99,
        "soft_limit": 99
      },
      "source_controls": [
        {
          "control_key": "1",
          "display_name": "Rose RS520",
          "supports_standby": true,
          "status": "selected"
        }
      ]
    }
  ],
  "state": "paused",
  "is_next_allowed": true,
  "is_previous_allowed": true,
  "is_pause_allowed": false,
  "is_play_allowed": true,
  "is_seek_allowed": true,
  "queue_items_remaining": 587,
  "queue_time_remaining": 147238,
  "settings": {
    "loop": "disabled",
    "shuffle": false,
    "auto_radio": true
  },
  "now_playing": {
    "seek_position": 58,
    "length": 253,
    "one_line": {
      "line1": "(I Know) What You Want - Andras & Oscar"
    },
    "two_line": {
      "line1": "(I Know) What You Want",
      "line2": "Andras & Oscar"
    },
    "three_line": {
      "line1": "(I Know) What You Want",
      "line2": "Andras & Oscar",
      "line3": "Ministry of Sound: Chillout Sessions XVIII"
    },
    "image_key": "6bcd6da481bce8871004b16188a173a9"
  },
  "seek_position": 58
}
*/

void RoonCtrl::processNewMessage(const string &msg)
{
    cDebugDom("roon") << "RoonCtrl: " << msg;
    Json jstate = Json::parse(msg);

    RoonPlayerState state = jstate.get<RoonPlayerState>();

    if (!state.zone_id.empty() &&
        subscribeCb.find(state.zone_id) != subscribeCb.end())
    {
        subscribeCb[state.zone_id](state);
    }
}

void RoonCtrl::subscribeZone(const string &zoneId, std::function<void(const RoonPlayerState &)> cb)
{
    cDebugDom("roon") << "RoonCtrl: subscribeZone: " << zoneId;

    //save cb for later
    subscribeCb[zoneId] = cb;

    Json root = {
        {"action", "subscribe"},
        {"zone_id", zoneId}
    };

    process->sendMessage(root.dump());
}

void RoonCtrl::sendMessage(const string &msg)
{
    process->sendMessage(msg);
}

RoonPlayer::RoonPlayer(Params &p):
    AudioPlayer(p)
{
    ioDoc->friendlyNameSet("Roon");
    ioDoc->aliasAdd("roon");
    ioDoc->descriptionSet(_("Roon audio player allows control of a Roon player from Calaos"));

    ioDoc->paramAdd("zone_id", _("Roon zone ID"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("host", _("Static Roon server IP address, empty to autodetect on network"), IODoc::TYPE_STRING, false);
    ioDoc->paramAdd("port", _("Static Roon server port, empty to autodetect on network"), IODoc::TYPE_INT, 9330);

    host = param["host"];
    zoneId = param["zone_id"];
    id = param["id"];
    Utils::from_string(param["port"], port);

    if (zoneId.empty())
    {
        cErrorDom("roon") << "RoonPlayer: zone name is empty!";
        return;
    }

    //start process
    RoonCtrl::Instance(host, port);

    //wait for the process to start
    Timer::singleShot(10, [this]()
    {
        RoonCtrl::Instance(host, port)->subscribeZone(zoneId, sigc::mem_fun(*this, &RoonPlayer::state_update_cb));
    });

    cInfoDom("roon") << "RoonPlayer created: " << zoneId;
}

RoonPlayer::~RoonPlayer()
{
}

void RoonPlayer::Play()
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): Play";

    Json root = {
        {"action", "play"},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::Pause()
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): Pause";

    Json root = {
        {"action", "pause"},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::Stop()
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): Stop";

    Json root = {
        {"action", "stop"},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::Next()
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): Next";

    Json root = {
        {"action", "next"},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::Previous()
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): Previous";

    Json root = {
        {"action", "previous"},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::state_update_cb(const RoonPlayerState &state)
{
    if (state.now_playing.one_line.line1 != playerState.now_playing.one_line.line1 ||
        state.now_playing.two_line.line1 != playerState.now_playing.two_line.line1 ||
        state.now_playing.two_line.line2 != playerState.now_playing.two_line.line2 ||
        state.now_playing.three_line.line1 != playerState.now_playing.three_line.line1 ||
        state.now_playing.three_line.line2 != playerState.now_playing.three_line.line2 ||
        state.now_playing.three_line.line3 != playerState.now_playing.three_line.line3 ||
        state.now_playing.image_key != playerState.now_playing.image_key ||
        state.now_playing.length.value_or(0) != playerState.now_playing.length.value_or(0))
    {
        set_status(AudioSongChange);

        EventManager::create(CalaosEvent::EventAudioSongChanged,
                             { { "player_id", id } });
    }
    else if (state.state != playerState.state ||
             state.now_playing.seek_position.value_or(0) != playerState.now_playing.seek_position.value_or(0) ||
             state.now_playing.length.value_or(0) != playerState.now_playing.length.value_or(0))
    {
        string stateplayer = "stop";
        if (state.state == "playing")
        {
            set_status(AudioPlay);
            stateplayer = "play";
        }
        else if (state.state == "paused")
        {
            set_status(AudioPause);
            stateplayer = "pause";
        }
        else if (state.state == "stopped")
        {
            set_status(AudioStop);
            stateplayer = "stop";
        }

        EventManager::create(CalaosEvent::EventAudioStatusChanged,
                             { { "player_id", id },
                               { "state", stateplayer } });
    }
    else if (state.outputs.size() > 0 && state.outputs[0].volume.value != playerState.outputs[0].volume.value)
    {
        set_status(AudioVolumeChange);

        int vol_percent = (state.outputs[0].volume.value * 100) / state.outputs[0].volume.max;

        EventManager::create(CalaosEvent::EventAudioVolumeChanged,
                             { { "player_id", id },
                               { "volume", Utils::to_string(vol_percent) } });
    }

    //update current player state
    playerState = state;
}

void RoonPlayer::get_volume(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_volume_cb(true, "", "", data);
    });
}

void RoonPlayer::get_volume_cb(bool status, string request, string result, AudioPlayerData data)
{
    if (playerState.outputs.size() > 0)
        data.get_chain_data().ivalue = playerState.outputs[0].volume.value;
    else
        data.get_chain_data().ivalue = 0;

    data.callback(data.get_chain_data());
}

void RoonPlayer::set_volume(int vol)
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): set_volume: " << vol;

    Json root = {
        {"action", "set_volume"},
        {"volume", vol},
        {"zone_id", zoneId}
    };

    RoonCtrl::Instance(host, port)->sendMessage(root.dump());
}

void RoonPlayer::get_songinfo(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_songinfo_cb(true, "", "", data);
    });
}

void RoonPlayer::get_songinfo_cb(bool status, string request, string result, AudioPlayerData data)
{
    data.get_chain_data().params.Add("title", playerState.now_playing.three_line.line1);
    data.get_chain_data().params.Add("artist", playerState.now_playing.three_line.line2);
    data.get_chain_data().params.Add("album", playerState.now_playing.three_line.line3);
    data.get_chain_data().params.Add("duration", Utils::to_string(playerState.now_playing.length.value_or(0)));

    data.callback(data.get_chain_data());
}

void RoonPlayer::get_current_time(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_current_time_cb(true, "", "", data);
    });
}

void RoonPlayer::get_current_time_cb(bool status, string request, string result, AudioPlayerData data)
{
    data.get_chain_data().dvalue = playerState.seek_position.has_value()?
                                    playerState.seek_position.value() :
                                    playerState.now_playing.seek_position.value_or(0);
    data.callback(data.get_chain_data());
}

void RoonPlayer::set_current_time(double seconds)
{
    cDebugDom("roon") << "RoonPlayer(" << zoneId << "): set_current_time: " << seconds;
}

void RoonPlayer::get_status(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_status_cb(true, "", "", data);
    });
}

void RoonPlayer::get_status_cb(bool status, string request, string result, AudioPlayerData data)
{
    if (playerState.state == "playing")
        data.get_chain_data().ivalue = AudioPlay;
    else if (playerState.state == "paused")
        data.get_chain_data().ivalue = AudioPause;
    else if (playerState.state == "stopped")
        data.get_chain_data().ivalue = AudioStop;
    else
        data.get_chain_data().ivalue = AudioError;
    data.callback(data.get_chain_data());
}

void RoonPlayer::get_album_cover(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_album_cover_cb(true, "", "", data);
    });
}

void RoonPlayer::get_album_cover_cb(bool status, string request, string result, AudioPlayerData data)
{
    data.get_chain_data().svalue = playerState.cover_url;
    data.callback(data.get_chain_data());
}

void RoonPlayer::get_playlist_current(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_playlist_current_cb(true, "", "", data);
    });
}

void RoonPlayer::get_playlist_current_cb(bool status, string request, string result, AudioPlayerData data)
{
    data.get_chain_data().ivalue = 0;
    data.callback(data.get_chain_data());
}

void RoonPlayer::get_playlist_size(AudioRequest_cb callback, AudioPlayerData user_data)
{
    AudioPlayerData data;
    data.set_chain_data(new AudioPlayerData(user_data));
    data.callback = callback;

    Timer::singleShot(0, [this, data]()
    {
        get_playlist_size_cb(true, "", "", data);
    });
}

void RoonPlayer::get_playlist_size_cb(bool status, string request, string result, AudioPlayerData data)
{
    data.get_chain_data().ivalue = 0;
    data.callback(data.get_chain_data());
}
