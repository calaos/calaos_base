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
#include "AudioOutput.h"
#include "AudioPlayer.h"
#include "EventManager.h"

using namespace Calaos;

AudioOutput::AudioOutput(Params &p, AudioPlayer *_player):
    Output(p),
    player(_player),
    answer("")
{
    // Define IO documentation
    ioDoc->friendlyNameSet("AudioInput");
    ioDoc->descriptionSet(_("Audio input associated to an AudioPlayer"));
    ioDoc->paramAdd("host", _("Logitech media server IP address"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("id", _("Unique ID of squeezebox in LMS"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("port_cli", _("CLI port of LMS, default to 9090"), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("port_web", _("Web interface port of LMS, default to 9000."), IODoc::TYPE_INT, false);

    ioDoc->conditionAdd("onplay", _("Event when play is started"));
    ioDoc->conditionAdd("onpause", _("Event when pausing player"));
    ioDoc->conditionAdd("onstop", _("Event when stopping player"));
    ioDoc->conditionAdd("onsongchange", _("Event when a new song is being played"));
    ioDoc->conditionAdd("onplaylistchange", _("Event when a change in the current playlist happens"));
    ioDoc->conditionAdd("onvolumechange", _("Event when a change of volume happens"));

    ioDoc->actionAdd("play", _("Start playing"));
    ioDoc->actionAdd("pause", _("Pause player"));
    ioDoc->actionAdd("stop", _("Stop player"));
    ioDoc->actionAdd("next", _("Play next song in playlist"));
    ioDoc->actionAdd("previous", _("Play previous song in playlist"));
    ioDoc->actionAdd("power on", _("Switch player on"));
    ioDoc->actionAdd("power off", _("Switch player off"));
    ioDoc->actionAdd("sleep 10", _("Start sleep mode with X seconds"));
    ioDoc->actionAdd("sync <playerid>", _("Sync this player with an other"));
    ioDoc->actionAdd("unsync <playerid>", _("Stop sync of this player with an other"));
    ioDoc->actionAdd("play <argument>", _("Clear playlist and play argument. <argument> can be any of album_id:XX artist_id:XX playlist_id:XX, ..."));
    ioDoc->actionAdd("add <argument>", _("Add tracks to playlist. <argument> can be any of album_id:XX artist_id:XX playlist_id:XX, ..."));
    ioDoc->actionAdd("volume set 50", _("Set current volume"));
    ioDoc->actionAdd("volume up 1", _("Increase volume by a value"));
    ioDoc->actionAdd("volume down 1", _("Decrease volume by a value"));

    get_params().Add("gui_type", "audio_output");
    get_params().Add("visible", "false");
}

AudioOutput::~AudioOutput()
{
}

bool AudioOutput::set_value(std::string value)
{
    if (!isEnabled()) return true;

    string val = value;

    cInfoDom("output") << "AudioOutput(" << get_param("id") << "): got action \"" << val << "\"";

    //list of all available player functions
    if (val == "play")
        player->Play();
    else if (val == "pause")
        player->Pause();
    else if (val == "stop")
        player->Stop();
    else if (val == "next")
        player->Next();
    else if (val == "previous")
        player->Previous();
    else if (val == "power on")
        player->Power(true);
    else if (val == "power off")
        player->Power(false);
    else if (val.compare(0, 6, "sleep ") == 0)
    {
        val.erase(0, 6);
        player->Sleep(atoi(val.c_str()));
    }
    else if (val.compare(0, 5, "sync ") == 0)
    {
        val.erase(0, 5);
        player->Synchronize(val.c_str(), true);
    }
    else if (val.compare(0, 7, "unsync ") == 0)
    {
        val.erase(0, 7);
        player->Synchronize(val.c_str(), false);
    }
    else if (val.compare(0, 5, "play ") == 0)
    {
        val.erase(0, 5);
        player->playlist_play_items(val);
    }
    else if (val.compare(0, 4, "add ") == 0)
    {
        val.erase(0, 4);
        player->playlist_add_items(val);
    }
    else if (val.compare(0, 11, "volume set ") == 0)
    {
        val.erase(0, 11);
        int vol;
        from_string(val, vol);

        player->set_volume(vol);
    }
    else if (val.compare(0, 10, "volume up ") == 0)
    {
        val.erase(0, 10);
        AudioPlayerData data;
        data.svalue = val;

        player->get_volume(sigc::mem_fun(*this, &AudioOutput::get_volume_cb), data);
    }
    else if (val.compare(0, 12, "volume down ") == 0)
    {
        val.erase(0, 12);
        AudioPlayerData data;
        data.svalue = "-" + val;

        player->get_volume(sigc::mem_fun(*this, &AudioOutput::get_volume_cb), data);
    }

    EventManager::create(CalaosEvent::EventOutputChanged,
                         { { "id", get_param("id") },
                           { "state", value } });

    return true;
}

std::string AudioOutput::get_value_string()
{
    return answer;
}

void AudioOutput::get_volume_cb(AudioPlayerData data)
{
    int vol;
    from_string(data.svalue, vol);

    player->set_volume(data.ivalue + vol);
}
