/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef EVENTS_H
#define EVENTS_H

#include "Timer.h"
#include "Calaos.h"
#include "Jansson_Addition.h"

/*
 * This class handles all events that goes out of calaos_server, like IO changes
 * that are dispatched to all clients. only "externaly available" events are
 * handled here.
 */

class CalaosEvent
{
public:
    enum
    {
        EventUnkown = 0,

        EventIOAdded,
        EventIODeleted,
        EventIOChanged,
        EventIOPropertyDelete,

        EventRoomAdded,
        EventRoomDeleted,
        EventRoomChanged,
        EventRoomPropertyDelete,

        EventTimeRangeChanged,
        EventScenarioAdded,
        EventScenarioDeleted,
        EventScenarioChanged,

        EventAudioSongChanged, //current playing song changed
        EventAudioPlaylistAdd, //new tracks added to playlist
        EventAudioPlaylistDelete, //tracks deleted from playlist
        EventAudioPlaylistMove, //tracks moved
        EventAudioPlaylistReload,
        EventAudioPlaylistCleared, //playlist has been cleared

        EventAudioStatusChanged, //player status changed
        EventAudioVolumeChanged,

        EventTouchScreenCamera, //event sent to a client to wake up and display required camera on screen

        EventPushNotification, //event that is sent to a mobile device
    };

    CalaosEvent();

    int getType() const { return evType; }
    const Params &getParam() const { return evParams; }

    json_t *toJson() const;
    string toString() const;

    static string typeToString(int type);

private:
    int evType = EventUnkown;
    Params evParams;

    friend class EventManager;
};

class EventManager: public sigc::trackable
{
public:
    static EventManager &Instance()
    {
        static EventManager inst;
        return inst;
    }
    ~EventManager();

    sigc::signal<void, const CalaosEvent &> newEvent;

    //create an event and give it to the EventManager
    static CalaosEvent create(int type);
    static CalaosEvent create(int type, Params p);

private:
    EventManager();

    void appendEvent(const CalaosEvent &ev);
    queue<CalaosEvent> eventsQueue;
};

#endif
