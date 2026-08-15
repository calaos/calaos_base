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
#include "ListeRoom.h"
#include "AutoScenario.h"
#include "CalaosConfig.h"

using namespace Calaos;

ListeRoom &ListeRoom::Instance()
{
    static ListeRoom inst;

    return inst;
}

ListeRoom::ListeRoom()
{
}

ListeRoom::~ListeRoom()
{
    while (rooms.size() > 0)
    {
        delete rooms[0];
        rooms.erase(rooms.begin());
    }
}

void ListeRoom::addIOHash(IOBase *io)
{
    if (!io) return;

    string id = io->get_param("id");

    //Deliberately NOT routed through findIO(): this check must keep working
    //for the degenerate "" key that a malformed io.xml can produce (findIO()
    //refuses to resolve it), otherwise a second id-less IO would silently
    //overwrite the entry of the first one.
    auto it = io_table.find(id);
    if (it != io_table.end() && it->second != io)
    {
        cErrorDom("root") << "addIOHash(): duplicate IO id '" << id
                          << "', an IO with this id is already registered, "
                          << "rejecting the new one to keep the existing one authoritative";
        return;
    }

    io_table[id] = io;

    if (io->get_param("gui_type") == "camera" &&
        find(cameraCache.begin(), cameraCache.end(), io) == cameraCache.end())
        cameraCache.push_back(io);
    else if (io->get_param("gui_type") == "audio_player" &&
             find(audioCache.begin(), audioCache.end(), io) == audioCache.end())
        audioCache.push_back(io);
}

void ListeRoom::delIOHash(IOBase *io)
{
    if (!io) return;

    //Only erase the io_table entry if it still points to this exact IO.
    //An IO whose id collided at addIOHash() time was never inserted, so
    //erasing by id alone here would silently drop the entry of the other,
    //still-alive IO that legitimately owns that id.
    auto entryIt = io_table.find(io->get_param("id"));
    if (entryIt != io_table.end() && entryIt->second == io)
        io_table.erase(entryIt);

    if (io->get_param("gui_type") == "camera")
    {
        auto it = find(cameraCache.begin(), cameraCache.end(), io);
        if (it != cameraCache.end())
            cameraCache.erase(it);
    }
    else if (io->get_param("gui_type") == "audio_player")
    {
        auto it = find(audioCache.begin(), audioCache.end(), io);
        if (it != audioCache.end())
            audioCache.erase(it);
    }
}

void ListeRoom::Add(Room *p)
{
    rooms.push_back(p);

    cDebugDom("room") << p->get_name() << "," << p->get_type();
}

void ListeRoom::Remove(int pos)
{
    //An out of range index used to walk the iterator past end() and delete
    //rooms[pos] out of bounds. Same guard style as the resolution accessors:
    //a miss is a no-op, never an unchecked access.
    if (pos < 0 || (uint)pos >= rooms.size())
    {
        cErrorDom("root") << "Remove(): no room at index " << pos
                          << " (" << rooms.size() << " rooms), ignoring";
        return;
    }

    vector<Room *>::iterator iter = rooms.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    delete rooms[pos];
    rooms.erase(iter);

    cDebugDom("room");
}

Room *ListeRoom::operator[] (int i) const
{
    if (i < 0 || (uint)i >= rooms.size())
        return nullptr;

    return rooms[i];
}

Room *ListeRoom::get_room(int i)
{
    if (i < 0 || (uint)i >= rooms.size())
        return nullptr;

    return rooms[i];
}

//See the contract documented on the declaration in ListeRoom.h.
IOBase *ListeRoom::findIO(const std::string &id) const
{
    //An empty id is not an identity, never resolve it (see the header).
    if (id.empty())
        return nullptr;

    //Single lookup, and const_iterator: operator[] on a missing key would
    //default-insert a null entry into io_table and inflate get_io_count().
    auto it = io_table.find(id);
    if (it == io_table.end())
        return nullptr;

    return it->second;
}

bool ListeRoom::hasIO(const std::string &id) const
{
    return findIO(id) != nullptr;
}

//Positional resolution. The nesting order (rooms in declaration order, then
//IOs in their in-room order) is the server-wide IO iteration order: it drives
//the order in which rules see their inputs. Do not reorder.
IOBase *ListeRoom::findIOByIndex(int index)
{
    if (index < 0)
        return nullptr;

    int cpt = 0;

    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size();m++)
        {
            IOBase *io = rooms[j]->get_io(m);
            if (cpt == index)
                return io;

            cpt++;
        }
    }

    return nullptr;
}

Room *ListeRoom::findRoomOfIO(const std::string &id)
{
    //Guarded composition: an unknown id must not reach getRoomByIO(), where a
    //null needle could match a null slot of some room's IO list.
    IOBase *io = findIO(id);
    if (!io)
        return nullptr;

    return getRoomByIO(io);
}

IOBase *ListeRoom::get_io(std::string id)
{
    return findIO(id);
}

IOBase *ListeRoom::get_io(int i)
{
    return findIOByIndex(i);
}

bool ListeRoom::delete_io(IOBase *io, bool del)
{
    bool done = false;
    for (uint j = 0;!done && j < rooms.size();j++)
    {
        for (int m = 0;!done && m < get_room(j)->get_size();m++)
        {
            IOBase *delio = get_room(j)->get_io(m);
            if (delio == io)
            {
                get_room(j)->RemoveIO(m, del);
                done = true;
            }
        }
    }

    return done;
}

int ListeRoom::get_io_count()
{
    return io_table.size();
}

IOBase *ListeRoom::get_chauffage_var(std::string &chauff_id, ChauffType type)
{
    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size();m++)
        {
            IOBase *io = rooms[j]->get_io(m);
            //Lookup result, never dereferenced unguarded (T2.18 style).
            if (!io) continue;

            if (io->get_param("chauffage_id") == chauff_id)
            {
                switch (type)
                {
                case PLAGE_HORAIRE: if (io->get_param("gui_type") == "time_range") return io; break;
                case CONSIGNE: if (io->get_param("gui_type") == "var_int") return io; break;
                case ACTIVE: if (io->get_param("gui_type") == "var_bool") return io; break;
                }
            }
        }
    }

    return nullptr;
}

void ListeRoom::addScenarioCache(Scenario *sc)
{
    auto_scenario_cache.push_back(sc);
}

void ListeRoom::delScenarioCache(Scenario *sc)
{
    auto_scenario_cache.remove(sc);
}

list<Scenario *> ListeRoom::getAutoScenarios()
{
    cDebugDom("room") << "Found " << auto_scenario_cache.size() << " auto_scenarios.";

    return auto_scenario_cache;
}

void ListeRoom::checkAutoScenario()
{
    list<Scenario *>::iterator it = auto_scenario_cache.begin();

    for (;it != auto_scenario_cache.end();it++)
    {
        Scenario *sc = *it;
        if (sc->getAutoScenario())
            sc->getAutoScenario()->checkScenarioRules();
    }

    list<Rule *> to_remove;
    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *rule = ListeRule::Instance().get_rule(i);
        if (rule->param_exists("auto_scenario") && !rule->isAutoScenario())
            to_remove.push_back(rule);
    }

    list<Rule *>::iterator itr = to_remove.begin();
    for (;itr != to_remove.end();itr++)
        ListeRule::Instance().Remove(*itr);

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();
}

Room * ListeRoom::searchRoomByNameAndType(string name, string type)
{
    Room *r = NULL;
    vector<Room *>::iterator itRoom;

    for(itRoom = rooms.begin(); itRoom != rooms.end() && !r; itRoom++)
        if( (*itRoom)->get_name() == name && (*itRoom)->get_type() == type)
            r = *itRoom;

    return r;
}

Room *ListeRoom::getRoomByIO(IOBase *o)
{
    //A null needle is not "the room of no IO": refuse it up front rather than
    //let it match a null slot of some room's IO list.
    if (!o) return nullptr;

    Room *r = NULL;

    for (uint j = 0;j < rooms.size() && !r;j++)
    {
        for (int m = 0;m < rooms[j]->get_size() && !r;m++)
        {
            if (rooms[j]->get_io(m) == o)
                r = rooms[j];
        }
    }

    return r;
}

bool ListeRoom::deleteIO(IOBase *io, bool modify)
{
    //Most callers pass a resolution result straight in (get_io()/findIO(),
    //often through a dynamic_cast). A miss must be a plain false, not a
    //dereference of nullptr in the gui_type test below.
    if (!io)
    {
        cErrorDom("root") << "deleteIO(): called with no IO, nothing to delete";
        return false;
    }

    //first delete all rules using "input"
    if (!modify) //only deletes if modify is not set
        ListeRule::Instance().RemoveRule(io);

    //Remove input from polling list
    if (io->get_param("gui_type") == "time"
        || io->get_param("gui_type") == "temp"
        || io->get_param("gui_type") == "analog_in"
        || io->get_param("gui_type") == "time_range"
        || io->get_param("gui_type") == "timer")
        ListeRule::Instance().Remove(io);

    return ListeRoom::Instance().delete_io(io);
}

IOBase* ListeRoom::createIO(Params param, Room *room)
{
    IOBase *io = nullptr;

    //A null room used to crash below on room->AddIO(). It happens when an
    //auto scenario IO is not attached to any room (getRoomByIO() miss).
    if (!room)
    {
        cErrorDom("root") << "createIO(): no room to attach IO '"
                          << param["id"] << "' to, creation aborted";
        return nullptr;
    }

    if (!param.Exists("name")) param.Add("name", "<No Name>");
    if (!param.Exists("type")) return nullptr;
    if (!param.Exists("id")) param.Add("id", Calaos::get_new_id("io_"));

    std::string type = param["type"];
    std::string id = param["id"];

    io = IOFactory::Instance().CreateIO(type, param);

    //E4.2b note: an explicitly empty id is let through here exactly as before
    //(findIO() would refuse to resolve it and the IO would be destroyed as a
    //false duplicate). Rejecting id-less IOs at creation is a semantic change
    //that belongs with the ownership move, not with this preparation ticket.
    if (io && !id.empty() && findIO(id) != io)
    {
        //addIOHash() rejected this IO because its id collided with an
        //already registered one. The object was still fully built by
        //IOFactory, so it must not be left half-added: never attach it to
        //room, and destroy it so it isn't reachable from anywhere while
        //being absent from io_table.
        cErrorDom("root") << "createIO(): discarding IO '" << id
                          << "', duplicate id was rejected by addIOHash()";
        delete io;
        io = nullptr;
    }

    if (io)
    {
        room->AddIO(io);

        EventManager::create(CalaosEvent::EventIOAdded,
                             { { "id", id },
                               { "room_name", room->get_name() },
                               { "room_type", room->get_type() } });
    }

    return io;
}
