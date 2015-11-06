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
#include <AudioPlayer.h>

using namespace Calaos;

AudioPlayer::AudioPlayer(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    database(nullptr)
{
    // Define IO documentation
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
    ioDoc->actionAdd("volume set 50", _("Set current volume"));
    ioDoc->actionAdd("volume up 1", _("Increase volume by a value"));
    ioDoc->actionAdd("volume down 1", _("Decrease volume by a value"));

    ioDoc->paramAdd("visible", _("Audio players are not displayed in rooms"), IODoc::TYPE_BOOL, true, "false", true);

    get_params().Add("gui_type", "audio_player");
    get_params().Add("visible", "false");
}

AudioPlayer::~AudioPlayer()
{
}

bool AudioPlayer::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:audio");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        param.get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

void AudioPlayer::hasChanged()
{
    if (!isEnabled()) return;

    string answer;

    switch (astatus)
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

    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", answer } });
}

void AudioPlayer::set_status(int st)
{
    astatus = st;
    hasChanged();
}

bool AudioPlayer::set_value(std::string value)
{
    if (!isEnabled()) return true;

    string val = value;

    cInfoDom("output") << "AudioPlayer(" << get_param("id") << "): got action \"" << val << "\"";

    //list of all available player functions
    if (val == "play")
        Play();
    else if (val == "pause")
        Pause();
    else if (val == "stop")
        Stop();
    else if (val == "next")
        Next();
    else if (val == "previous")
        Previous();
    else if (val == "power on")
        Power(true);
    else if (val == "power off")
        Power(false);
    else if (val.compare(0, 6, "sleep ") == 0)
    {
        val.erase(0, 6);
        Sleep(atoi(val.c_str()));
    }
    else if (val.compare(0, 5, "sync ") == 0)
    {
        val.erase(0, 5);
        Synchronize(val.c_str(), true);
    }
    else if (val.compare(0, 7, "unsync ") == 0)
    {
        val.erase(0, 7);
        Synchronize(val.c_str(), false);
    }
    else if (val.compare(0, 5, "play ") == 0)
    {
        val.erase(0, 5);
        playlist_play_items(val);
    }
    else if (val.compare(0, 4, "add ") == 0)
    {
        val.erase(0, 4);
        playlist_add_items(val);
    }
    else if (val.compare(0, 11, "volume set ") == 0)
    {
        val.erase(0, 11);
        int vol;
        from_string(val, vol);

        set_volume(vol);
    }
    else if (val.compare(0, 10, "volume up ") == 0)
    {
        val.erase(0, 10);
        AudioPlayerData data;
        data.svalue = val;

        get_volume(sigc::mem_fun(*this, &AudioPlayer::get_volume_cb), data);
    }
    else if (val.compare(0, 12, "volume down ") == 0)
    {
        val.erase(0, 12);
        AudioPlayerData data;
        data.svalue = "-" + val;

        get_volume(sigc::mem_fun(*this, &AudioPlayer::get_volume_cb), data);
    }

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", value } });

    return true;
}

void AudioPlayer::get_volume_cb(AudioPlayerData data)
{
    int vol;
    from_string(data.svalue, vol);

    set_volume(data.ivalue + vol);
}
