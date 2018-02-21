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
