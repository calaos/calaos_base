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
#ifndef S_ListeRoom_H
#define S_ListeRoom_H

#include "Calaos.h"
#include "Room.h"
#include "ListeRule.h"
#include "IOFactory.h"
#include "Scenario.h"

namespace Calaos
{

typedef enum { PLAGE_HORAIRE, CONSIGNE, ACTIVE } ChauffType;

class ListeRoom
{
protected:
    std::vector<Room *> rooms;
    unordered_map<string, IOBase *> io_table;

    list<IOBase *> cameraCache;
    list<IOBase *> audioCache;

    list<Scenario *> auto_scenario_cache;

    ListeRoom();

public:
    //singleton
    static ListeRoom &Instance();

    ~ListeRoom();

    void Add(Room *p);
    void Remove(int i);
    Room *get_room(int i);
    Room *operator[] (int i) const;

    /* -------------------------------------------------------------------
     * Resolution accessors (E4.2a)
     *
     * Single entry point for every "give me the IO whose id is X" question.
     * The contract, which the rest of the tree may rely on:
     *   - the returned pointer is NON-OWNING. The IO is owned by its Room;
     *     the pointer is only valid until that IO is destroyed. Holding it
     *     across a delete/reload is what E4.2 is about, and is still the
     *     caller's problem at this stage.
     *   - an unknown id yields nullptr, explicitly. The lookup never inserts
     *     into io_table (no operator[] on a missing key) and never
     *     dereferences what it found.
     *   - an empty id is not an identity: it always resolves to nullptr,
     *     even in the degenerate case of a malformed io.xml having pushed an
     *     id-less IO into io_table under the "" key. That accidental entry
     *     stays correctly book-kept by addIOHash()/delIOHash(), it is simply
     *     not addressable.
     *   - resolution is by id only: it never scans the rooms, so an IO that
     *     addIOHash() rejected as a duplicate is invisible here (by design,
     *     the first registered IO stays authoritative).
     * ---------------------------------------------------------------- */
    IOBase *findIO(const std::string &id) const;

    /* Presence test that never hands out a pointer, for call sites that only
       need to know whether an id is taken. */
    bool hasIO(const std::string &id) const;

    /* findIO() + dynamic_cast, i.e. the pattern spelled out by hand at ~13
       call sites (JsonApi, AutoScenario, Rules/…). nullptr both when the id
       is unknown and when the IO exists but is not a T: a miss is never
       distinguishable from a type mismatch, and neither is dereferenced. */
    template<typename T>
    T *findIOAs(const std::string &id) const { return dynamic_cast<T *>(findIO(id)); }

    /* Positional resolution over the rooms, in room order then in-room order.
       That order is the IO iteration order of the whole server and MUST NOT
       change. Out of range yields nullptr. */
    IOBase *findIOByIndex(int index);

    /* Room owning the IO with this id, or nullptr if either is unknown.
       Composition of findIO() and getRoomByIO(), with no deref in between. */
    Room *findRoomOfIO(const std::string &id);

    /* Historical names, kept so the ~60 existing call sites are untouched.
       Thin forwarders to the accessors above. */
    IOBase *get_io(std::string id);
    IOBase *get_io(int i);
    bool delete_io(IOBase *io, bool del = true);

    int get_io_count(); //total IO count for all rooms

    int size() { return rooms.size(); }

    IOBase *get_chauffage_var(std::string &chauff_id, ChauffType type);

    list<IOBase *> getCameraList() { return cameraCache; }
    list<IOBase *> getAudioList() { return audioCache; }

    //Auto scenarios

    void addScenarioCache(Scenario *sc);
    void delScenarioCache(Scenario *sc);
    list<Scenario *> getAutoScenarios();
    void checkAutoScenario();

    Room * searchRoomByNameAndType(string name,string type);

    Room *getRoomByIO(IOBase *o);

    bool deleteIO(IOBase *io, bool modify = false);

    IOBase* createIO(Params param, Room *room);

    void addIOHash(IOBase *io);
    void delIOHash(IOBase *io);
};

}

#endif
