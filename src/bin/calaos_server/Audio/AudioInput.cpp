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
#include <AudioInput.h>
#include <AudioPlayer.h>
#include <IPC.h>

using namespace Calaos;

AudioInput::AudioInput(Params &p, AudioPlayer *_player):
                Input(p),
                answer(""),
                status(ERROR),
                player(_player)
{
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
                  case ERROR: answer = "onerror"; break;
                  case PLAY: answer = "onplay"; break;
                  case PAUSE: answer = "onpause"; break;
                  case STOP: answer = "onstop"; break;
                  case SONG_CHANGE: answer = "onsongchange"; break;
                  case PLAYLIST_CHANGE: answer = "onplaylistchange"; break;
                  case VOLUME_CHANGE: answer = "onvolumechange"; break;
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
