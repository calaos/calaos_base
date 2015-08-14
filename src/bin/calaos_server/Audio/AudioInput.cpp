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
#include "AudioInput.h"
#include "AudioPlayer.h"
#include "EventManager.h"

using namespace Calaos;

AudioInput::AudioInput(Params &p, AudioPlayer *_player):
    Input(p),
    player(_player),
    answer(""),
    status(AudioError)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("AudioInput");
    ioDoc->descriptionSet(_("Audio input associated to an AudioPlayer"));
    ioDoc->paramAdd("host", _("Logitech media server IP address"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("id", _("Unique ID of squeezebox in LMS"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port_cli", _("CLI port of LMS, default to 9090"), 0, 65535, false);
    ioDoc->paramAddInt("port_web", _("Web interface port of LMS, default to 9000."), 0, 65535, false);

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

    get_params().Add("gui_type", "audio_input");
    get_params().Add("visible", "false");
}

AudioInput::~AudioInput()
{
}

void AudioInput::hasChanged()
{
    if (!isEnabled()) return;

    //         if (st != status)
    {
        status = st;
        switch (status)
        {
        default:
        case AudioError: answer = "onerror"; break;
        case AudioPlay: answer = "onplay"; break;
        case AudioPause: answer = "onpause"; break;
        case AudioStop: answer = "onstop"; break;
        case AudioSongChange: answer = "onsongchange"; break;
        case AudioPlaylistChange: answer = "onplaylistchange"; break;
        case AudioVolumeChange: answer = "onvolumechange"; break;
        }

        EmitSignalInput();

        EventManager::create(CalaosEvent::EventInputChanged,
                             { { "id", get_param("id") },
                               { "state", answer } });
    }
}

std::string AudioInput::get_value_string()
{
    return answer;
}
