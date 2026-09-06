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
    //Before any IO goes: an AutoScenario elsewhere may still name this room,
    //and `autoscenario modify` really does move a scenario IO out of the room
    //that keeps its machinery.
    ListeRoom::Instance().forgetRoomInAutoScenarios(this);

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
        /* T3.18: Destroy, explicitly - line "Room::~Room -> detachIOFromRules()
         * / arret du serveur" of the deleteIO() census. The room and every one
         * of its IOs are going away for good, so there is nobody left to warn
         * and nothing left to re-enable; keeping the rules would leave them
         * pointing at IOs that no longer exist anywhere in the tree.
         */
        ListeRoom::Instance().detachIOFromRules(ios[0].get(), false,
                                                RuleDetachPolicy::Destroy);
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

    //Take the owner OUT of the vector before anything can be destroyed, the
    //same way ListeRoom::Remove() does for a Room. Destroying inside erase()
    //would run ~IOBase() while `ios` is mid-shuffle: vector::erase
    //move-assigns the tail down and unique_ptr::operator= is
    //reset(u.release()), so the destructor would see a container of unchanged
    //size whose slot `pos` already holds the NEXT IO and whose last slot is
    //null. Nothing in the tree walks Room::ios from an IO destructor today,
    //but the whole point of this ticket is that the window should not exist.
    std::unique_ptr<IOBase> owned = std::move(ios[pos]);
    ios.erase(ios.begin() + pos);

    //The auto scenarios hold raw pointers to the IOs that drive them. This is
    //the one place every destruction goes through, transfers excepted - and a
    //transfer must NOT be forgotten, the IO goes on living.
    if (del)
        ListeRoom::Instance().forgetIOInAutoScenarios(owned.get());

    //del == false is an ownership TRANSFER to the caller, not a discreet
    //removal: the caller goes on using the IO and becomes responsible for
    //destroying it, so let the pointer go instead of destroying it.
    //Otherwise `owned` destroys the IO at the end of this scope, with `ios`
    //already fully consistent.
    if (!del)
        (void)owned.release();
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

bool Room::LoadFromXml(pugi::xml_node room_node)
{
    pugi::xml_node node = XmlUtils::firstChildElement(room_node);
    for(; node; node = XmlUtils::nextSiblingElement(node))
    {
        const string nodeName = node.name();
        if (nodeName == "calaos:input" ||
            nodeName == "calaos:output" ||
            nodeName == "calaos:internal" ||
            nodeName == "calaos:avr" ||
            nodeName == "calaos:camera" ||
            nodeName == "calaos:audio" ||
            nodeName == "calaos:remote_ui")
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

bool Room::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node room_node = node.append_child("calaos:room");
    XmlUtils::setAttribute(room_node, "name", name);
    XmlUtils::setAttribute(room_node, "type", type);
    XmlUtils::setAttribute(room_node, "hits", Utils::to_string(hits));

    for (int i = 0;i < get_size();i++)
    {
        IOBase *io = get_io(i);
        //Kept deliberately. It is NOT a workaround for a transient null slot
        //during RemoveIO() — that window no longer exists, the IO is moved
        //out of the vector before being destroyed. It is the plain T2.18 rule
        //applied to what is now a documented-nullable accessor: get_io() may
        //return nullptr, and this is the only place in this file that
        //dereferences its result. In range it cannot fire (AddIO() refuses
        //null and no slot is ever empty), so it costs one predictable test.
        if (!io) continue;

        io->SaveToXml(room_node);
    }

    return true;
}
