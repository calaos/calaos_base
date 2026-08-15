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
    //E4.2b: the room owns its IOs, so destroying it destroys them. The rule
    //bookkeeping that ListeRoom::deleteIO() used to run on our behalf still
    //has to happen, and in the same order (rules dropped first, then the
    //EventIODeleted, then the object). It is done here directly instead of
    //through ListeRoom::deleteIO(), because that round trip needed ListeRoom
    //to locate *this* room in its rooms vector: a destructor cannot rely on
    //still being reachable from there, and when it was not, the old loop
    //never shrank `ios` and spun forever.
    while (!ios.empty())
    {
        ListeRoom::Instance().detachIOFromRules(ios[0].get());
        RemoveIO(0, true);
    }
}

void Room::AddIO(IOBase *io)
{
    //Ownership transfer in. A null IO used to be pushed into the list and
    //then dereferenced right below.
    if (!io)
    {
        cErrorDom("room") << "AddIO(): ignoring a null IO";
        return;
    }

    ios.emplace_back(io);

    cDebugDom("room") << "(" << io->get_param("id") << "): Ok";
}

void Room::RemoveIO(int pos, bool del)
{
    if (pos < 0 || (size_t)pos >= ios.size())
    {
        cErrorDom("room") << "RemoveIO(): no IO at index " << pos
                          << " (" << ios.size() << " IOs), ignoring";
        return;
    }

    EventManager::create(CalaosEvent::EventIODeleted,
                         { { "id", ios[pos]->get_param("id") },
                           { "room_name", get_name() },
                           { "room_type", get_type() } });

    //del == false is an ownership TRANSFER to the caller, not a discreet
    //removal: the caller goes on using the IO and becomes responsible for
    //destroying it. release() hands the pointer over and leaves an empty
    //unique_ptr behind, so the erase() below destroys nothing.
    if (!del)
        (void)ios[pos].release();

    ios.erase(ios.begin() + pos);
}

void Room::RemoveIOFromRoom(IOBase *io)
{
    auto it = find_if(ios.begin(), ios.end(),
                      [io](const std::unique_ptr<IOBase> &p) { return p.get() == io; });
    if (it != ios.end())
    {
        //Ownership transfer as well (see the header): the caller re-attaches
        //the IO to another room with AddIO(). release(), not a plain erase of
        //a live pointer.
        (void)it->release();
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
            node->ValueStr() == "calaos:audio" ||
            node->ValueStr() == "calaos:remote_ui")
        {
            //CreateIO() returns a raw OWNING pointer (see IOFactory.h): park
            //it in a unique_ptr so it cannot leak between here and AddIO(),
            //which takes the ownership over.
            std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO(node));
            //Note: an IO whose id addIOHash() rejected as a duplicate is still
            //added to the room, exactly as before (ListeRoomRobustness pins
            //it): only ListeRoom::createIO() refuses half-added IOs.
            if (io) AddIO(io.release());
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
        if (!io) continue;

        io->SaveToXml(room_node);
    }

    return true;
}
