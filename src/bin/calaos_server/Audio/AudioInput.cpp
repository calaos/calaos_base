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
#include <AudioInput.h>
#include <AudioPlayer.h>
#include <IPC.h>

using namespace Calaos;

AudioInput::AudioInput(Params &p, AudioPlayer *_player):
    Input(p),
    player(_player),
    answer(""),
    status(AudioError)
{
    get_params().Add("gui_type", "audio_input");
    get_params().Add("visible", "false");
}

AudioInput::~AudioInput()
{
}

void AudioInput::hasChanged()
{
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

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += url_encode(string("state:") + answer);
        IPC::Instance().SendEvent("events", sig);
    }
}

std::string AudioInput::get_value_string()
{
    return answer;
}
