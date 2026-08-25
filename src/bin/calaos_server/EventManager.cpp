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

#include "EventManager.h"
#include "HistLogger.h"
#include "ListeRoom.h"

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
        //T3.40: EventManager is a function-local static (EventManager.h) and
        //nothing in the tree deletes it, so `this` outlives every idler this
        //loop can run. Left unguarded on that measured ground, not on the
        //assumption that "a singleton is safe".
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

    //Write events to the history logger
    if (ev.getType() == CalaosEvent::EventIOChanged &&
        ev.getParam().Exists("state") &&
        ev.logHistory)
    {
        string id = ev.getParam()["id"];
        IOBase *io = ListeRoom::Instance().get_io(id);

        if (!io)
        {
            cWarning() << "Failed to find IO " << id;
            return;
        }

        //Do not log IO that are not enabled
        if (io->get_param("log_history") != "true")
            return;

        /* E4.1l: the third wire of CalaosEvent::toJson(), and the one no test
         * had an ORACLE on before core/EventWireBytes_test.cpp. It was NOT
         * unexercised - the first delivery claimed that and it is false,
         * measured by instrumenting this very line: three preexisting suites
         * walk through here 13 times per make check, because seven IO classes
         * set log_history="true" by default in their own constructor. They
         * simply never asserted anything about the bytes it writes.
         * The three emission invariants of
         * the epic, all three together, exactly as on the two API emitters:
         * compact, ensure_ascii = true (this row stays pure ASCII, as it was
         * under JSON_ENSURE_ASCII; what changes on the bytes is the case of
         * the hexadecimal, U+007F which jansson emitted RAW and nlohmann
         * escapes, an embedded NUL which json_string() truncated at, and the
         * invalid-UTF-8 pair that used to vanish - see RELEASE_NOTES.md), and
         * error_handler_t::replace - NOT a try/catch. It matters here more than
         * anywhere: this state comes straight from set_state, including the
         * percent decoded GET parameter fallback of JsonApiHandlerHttp.cpp:88,
         * which never goes through any JSON parser. A bare dump() would throw
         * type_error.316 inside appendEvent(), where nothing catches it.
         * The old json_dumps() NULL branch is gone with it: dump() with the
         * replacing handler has no failure mode to test.
         */
        string jstr = ev.toJson().dump(-1, ' ', true, Json::error_handler_t::replace);

        HistEvent e = HistEvent::create();
        e.event_raw = jstr;
        e.event_type = ev.getType();
        e.io_id = id;
        e.io_state = ev.getParam()["state"];

        HistLogger::Instance().appendEvent(e);
    }
}

CalaosEvent EventManager::create(int type, bool logHistory)
{
    CalaosEvent ev;
    ev.evType = type;
    ev.logHistory = logHistory;

    EventManager::Instance().appendEvent(ev);

    return ev;
}

CalaosEvent EventManager::create(int type, Params p, bool logHistory)
{
    CalaosEvent ev;
    ev.evType = type;
    ev.evParams = p;
    ev.logHistory = logHistory;

    EventManager::Instance().appendEvent(ev);

    return ev;
}

CalaosEvent EventManager::create(int type, string ioId, Params p, bool logHistory)
{
    CalaosEvent ev;
    ev.evType = type;
    ev.evParams = p;
    ev.logHistory = logHistory;
    ev.evParams.Add("id", ioId);

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

    case EventTouchScreenCamera: return "touchscreen_camera_request";

    case EventPushNotification: return "push_notif";

    case EventIOStatusChanged: return "io_status_changed";

    default: break;
    }

    cError() << "Unkown string for event " << type;
    cError() << "Did you forget to add string representation for that event??";

    return "unkown";
}

Json CalaosEvent::toJson() const
{
    /* Where the silent pair drop really came from, MEASURED against the real
     * jansson rather than read: json_pack("{s:s,...}") does NOT drop a pair
     * whose C string is NULL - it answers NULL for the WHOLE OBJECT, and this
     * function would then have returned nothing at all. The drop happened one
     * level down, in jansson_from_params(): json_string() answers NULL on a
     * value that is not valid UTF-8, json_object_set_new() answers -1, neither
     * return code was tested, and only THAT pair vanished while the object
     * stayed valid. So the old failure modes were two, not one. A degenerate
     * event cannot lose a member here any more: the three scalars are always
     * built (typeToString() answers "unkown" rather than nothing, and
     * toString() url-encodes, so event_raw is ASCII whatever the parameters
     * are), and an invalid parameter value now reaches the wire as U+FFFD
     * instead of vanishing. Same arbitrage as E4.1j / F-LUA-2: both outcomes
     * are garbage and the entry was already broken in both.
     * Params is a std::map, so "data" is alphabetical here as it was before.
     */
    return Json{
        { "event_raw", toString() },
        { "type", Utils::to_string(getType()) },
        { "type_str", typeToString(getType()) },
        { "data", evParams.toNJson() }
    };
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
