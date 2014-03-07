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
#include <AudioOutput.h>
#include <AudioPlayer.h>
#include <IPC.h>

using namespace Calaos;

AudioOutput::AudioOutput(Params &p, AudioPlayer *_player):
                Output(p),
                player(_player),
                answer("")
{
        get_params().Add("gui_type", "audio_output");
        get_params().Add("visible", "false");
}

AudioOutput::~AudioOutput()
{
}

bool AudioOutput::set_value(std::string value)
{
        string val = value;

        cInfoDom("output") << "AudioOutput(" << get_param("id") << "): got action \"" << val << "\"" << log4cpp::eol;

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

        string sig = "output ";
        sig += get_param("id") + " ";
        sig += url_encode(string("state:") + value);
        IPC::Instance().SendEvent("events", sig);

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
