/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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

#include "EventManager.h"

EventManager::EventManager()
{
}

EventManager::~EventManager()
{
    //clear queue
    eventsQueue = queue<CalaosEvent>();
}

void EventManager::appendEvent(const CalaosEvent &ev)
{
    if (ev.getType() == CalaosEvent::EventUnkown)
    {
        cWarning() << "Event type Unkown added to queue, dropping...";
        return;
    }

    if (eventsQueue.empty())
    {
        //start idler if it was stopped
        Idler::singleIdler([=]()
        {
            while (!eventsQueue.empty())
            {
                CalaosEvent e = eventsQueue.front();
                eventsQueue.pop();

                newEvent.emit(e);
            }
        });
    }

    eventsQueue.push(ev);
}

CalaosEvent EventManager::create(int type)
{
    CalaosEvent ev;
    ev.evType = type;

    EventManager::Instance().appendEvent(ev);

    return ev;
}

CalaosEvent EventManager::create(int type, Params p)
{
    CalaosEvent ev;
    ev.evType = type;
    ev.evParams = p;

    EventManager::Instance().appendEvent(ev);

    return ev;
}

CalaosEvent::CalaosEvent()
{
}

string CalaosEvent::typeToString(int type)
{
    switch (type)
    {
    case EventIOAdded: return "io_added";
    case EventIODeleted: return "io_deleted";
    case EventIOChanged: return "io_changed";
    case EventIOPropertyDelete: return "io_prop_deleted";

    case EventRoomAdded: return "room_added";
    case EventRoomDeleted: return "room_deleted";
    case EventRoomChanged: return "room_changed";
    case EventRoomPropertyDelete: return "room_prop_deleted";

    case EventTimeRangeChanged: return "timerange_changed";
    case EventScenarioAdded: return "scenario_added";
    case EventScenarioDeleted: return "scenario_deleted";
    case EventScenarioChanged: return "scenario_changed";

    case EventAudioSongChanged: return "audio_song_changed";
    case EventAudioPlaylistAdd: return "playlist_tracks_added";
    case EventAudioPlaylistDelete: return "playlist_tracks_deleted";
    case EventAudioPlaylistMove: return "playlist_tracks_moved";
    case EventAudioPlaylistReload: return "playlist_reload";
    case EventAudioPlaylistCleared: return "playlist_cleared";

    case EventAudioStatusChanged: return "audio_status_changed";
    case EventAudioVolumeChanged: return "audio_volume_changed";

    default: break;
    }

    cError() << "Unkown string for event " << type;
    cError() << "Did you forget to add string representation for that event??";

    return "unkown";
}

json_t *CalaosEvent::toJson() const
{
    json_t *ret;

    ret = json_pack("{s:s, s:s, s:s, s:o}",
                    "event_raw", toString().c_str(),
                    "type", Utils::to_string(getType()).c_str(),
                    "type_str", typeToString(getType()).c_str(),
                    "data", evParams.toJson());

    return ret;
}

string CalaosEvent::toString() const
{
    string ret = typeToString(getType());

    for (int i = 0;i < evParams.size();i++)
    {
        string key, val;
        evParams.get_item(i, key, val);

        ret += " ";
        ret += Utils::url_encode(key);
        ret += ":";
        ret += Utils::url_encode(val);
    }

    return ret;
}
