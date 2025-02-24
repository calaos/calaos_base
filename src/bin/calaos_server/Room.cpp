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
#include "Room.h"
#include "ListeRule.h"
#include "ListeRoom.h"
#include "AVReceiver.h"

using namespace Calaos;

Room::Room(string _name, string _type, int _hits):
    name(_name),
    type(_type),
    hits(_hits)
{
    cDebugDom("room") << "Room::Room(" << name << ", " << type << "): Ok";
}

Room::~Room()
{
    while (ios.size() > 0)
        ListeRoom::Instance().deleteIO(ios[0]);

    ios.clear();
}

void Room::AddIO(IOBase *io)
{
    ios.push_back(io);

    cDebugDom("room") << "(" << io->get_param("id") << "): Ok";
}

void Room::RemoveIO(int pos, bool del)
{
    EventManager::create(CalaosEvent::EventIODeleted,
                         { { "id", ios[pos]->get_param("id") },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    vector<IOBase *>::iterator iter = ios.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    if (del) delete ios[pos];
    ios.erase(iter);
}

void Room::RemoveIOFromRoom(IOBase *io)
{
    vector<IOBase *>::iterator it = find(ios.begin(), ios.end(), io);
    if (it != ios.end())
    {
        ios.erase(it);

        EventManager::create(CalaosEvent::EventRoomChanged,
                             { { "input_id_deleted", io->get_param("id") },
                               { "room_name", get_name() },
                               { "room_type", get_type() } });
    }
}

void Room::set_name(std::string &s)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_name", get_name() },
                           { "new_room_name", s },
                           { "room_type", get_type() } });

    name = s;
}

void Room::set_type(std::string &s)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_type", get_type() },
                           { "new_room_type", s },
                           { "room_name", get_name() } });

    type = s;
}

void Room::set_hits(int h)
{
    EventManager::create(CalaosEvent::EventRoomChanged,
                         { { "old_room_hits", Utils::to_string(hits) },
                           { "new_room_hits", Utils::to_string(h) },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    hits = h;
}

bool Room::LoadFromXml(TiXmlElement *room_node)
{
    TiXmlElement *node = room_node->FirstChildElement();
    for(; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:input" ||
            node->ValueStr() == "calaos:output" ||
            node->ValueStr() == "calaos:internal" ||
            node->ValueStr() == "calaos:avr" ||
            node->ValueStr() == "calaos:camera" ||
            node->ValueStr() == "calaos:audio")
        {
            IOBase *io = IOFactory::Instance().CreateIO(node);
            if (io) AddIO(io);
        }
    }

    return true;
}

bool Room::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *room_node = new TiXmlElement("calaos:room");
    room_node->SetAttribute("name", name);
    room_node->SetAttribute("type", type);
    room_node->SetAttribute("hits", Utils::to_string(hits));
    node->LinkEndChild(room_node);

    for (int i = 0;i < get_size();i++)
    {
        IOBase *io = get_io(i);

        io->SaveToXml(room_node);
    }

    return true;
}
