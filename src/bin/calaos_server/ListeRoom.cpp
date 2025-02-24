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

    io_table[io->get_param("id")] = io;

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

    io_table.erase(io->get_param("id"));

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
    vector<Room *>::iterator iter = rooms.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    delete rooms[pos];
    rooms.erase(iter);

    cDebugDom("room");
}

Room *ListeRoom::operator[] (int i) const
{
    return rooms[i];
}

Room *ListeRoom::get_room(int i)
{
    return rooms[i];
}

IOBase *ListeRoom::get_io(std::string id)
{
    if (io_table.find(id) != io_table.end())
        return io_table[id];

    return nullptr;
}

IOBase *ListeRoom::get_io(int i)
{
    int cpt = 0;

    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size();m++)
        {
            IOBase *io = rooms[j]->get_io(m);
            if (cpt == i)
                return io;

            cpt++;
        }
    }

    return nullptr;
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

    if (!param.Exists("name")) param.Add("<No Name>", "Input");
    if (!param.Exists("type")) return nullptr;
    if (!param.Exists("id")) param.Add("id", Calaos::get_new_id("io_"));

    std::string type = param["type"];
    io = IOFactory::Instance().CreateIO(type, param);
    if (io)
        room->AddIO(io);

    EventManager::create(CalaosEvent::EventIOAdded,
                         { { "id", param["id"] },
                           { "room_name", room->get_name() },
                           { "room_type", room->get_type() } });

    return io;
}
