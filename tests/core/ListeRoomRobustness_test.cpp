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

//ListeRoom robustness tests (ticket T1.3): M6 (default name key/value were
//swapped), m9 (EventIOAdded fired even when creation failed) and m10
//(duplicate ids silently overwritten in io_table).
//
//Event delivery cannot be observed here: EventManager::create() only queues
//the event and starts a libuv idler to dispatch it later (see EventManager.cpp),
//and CalaosCoreFixture runs no libuv loop (RuleLifecycle_test.cpp confirms no
//core test ever tries to observe an EventManager signal). So m9 is exercised
//through its only observable side effect: a failed creation must not touch
//the room or io_table either, exactly like it must not announce a non-existent
//IO.

#include "CalaosCoreFixture.h"

using namespace Calaos;
using namespace CalaosTest;

class ListeRoomRobustnessTest: public CoreFixture
{
};

/******************************************************************************
 * M6 - default name
 ******************************************************************************/

//An IO created without a "name" param must get name="<No Name>", not a param
//literally named "<No Name>".
TEST_F(ListeRoomRobustnessTest, IoCreatedWithoutNameGetsTheDefaultName)
{
    loadConfig();

    Params p = { { "type", "InternalBool" }, { "id", "io_t13_no_name" } };
    IOBase *created = createIO(p, firstRoom());

    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->get_param("name"), "<No Name>");
    //The pre-fix bug added a param named "<No Name>" with value "Input"
    //instead: make sure that stray param is gone.
    EXPECT_EQ(created->get_param("<No Name>"), "");
}

/******************************************************************************
 * m9 - EventIOAdded only on success
 ******************************************************************************/

//createIO() failing (missing "type", or a type unknown to IOFactory) must
//leave the room and io_table untouched.
TEST_F(ListeRoomRobustnessTest, FailedIoCreationLeavesNothingBehind)
{
    loadConfig();

    int countBefore = ListeRoom::Instance().get_io_count();
    int sizeBefore = firstRoom()->get_size();

    Params noType = { { "id", "io_t13_no_type" } };
    EXPECT_EQ(createIO(noType, firstRoom()), nullptr);

    Params unknownType = { { "type", "ThisTypeDoesNotExist" }, { "id", "io_t13_unknown_type" } };
    EXPECT_EQ(createIO(unknownType, firstRoom()), nullptr);

    EXPECT_EQ(ListeRoom::Instance().get_io_count(), countBefore);
    EXPECT_EQ(firstRoom()->get_size(), sizeBefore);
    EXPECT_EQ(io("io_t13_no_type"), nullptr);
    EXPECT_EQ(io("io_t13_unknown_type"), nullptr);
}

/******************************************************************************
 * m10 - duplicate ids
 ******************************************************************************/

//Two IOs sharing the same id in io.xml: the second is rejected, the first
//stays reachable and get_io_count() is not inflated.
TEST_F(ListeRoomRobustnessTest, DuplicateIdInXmlIsRejectedAndFirstWins)
{
    std::string ios;
    ios += internalIoXml("InternalBool", "io_t13_dup", "First");
    ios += internalIoXml("InternalInt", "io_t13_dup", "Second");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    ASSERT_EQ(ListeRoom::Instance().get_io_count(), 1);

    IOBase *survivor = io("io_t13_dup");
    ASSERT_NE(survivor, nullptr);
    EXPECT_EQ(survivor->get_param("name"), "First");
    EXPECT_EQ(survivor->get_type(), TBOOL);

    //Documented boundary of this ticket: Room::LoadFromXml (owned by another
    //ticket) still calls AddIO() for any non-null IO regardless of whether
    //addIOHash() accepted it, so the rejected duplicate stays in the Room's
    //own list even though io_table correctly excludes it.
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->get_size(), 2);
}

//The real risk of a naive fix: deleting the rejected duplicate must not wipe
//out the surviving IO's io_table entry (delIOHash() must only erase an id
//when it still points at the IO being destroyed).
TEST_F(ListeRoomRobustnessTest, DeletingTheRejectedDuplicateLeavesTheSurvivorRegistered)
{
    std::string ios;
    ios += internalIoXml("InternalBool", "io_t13_dup2", "First");
    ios += internalIoXml("InternalInt", "io_t13_dup2", "Second");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    ASSERT_EQ(room->get_size(), 2);

    IOBase *survivor = io("io_t13_dup2");
    ASSERT_NE(survivor, nullptr);

    IOBase *duplicate = room->get_io(0) == survivor ? room->get_io(1) : room->get_io(0);
    ASSERT_NE(duplicate, survivor);

    ASSERT_TRUE(deleteIO(duplicate));

    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 1);
    EXPECT_EQ(io("io_t13_dup2"), survivor)
        << "deleting the rejected duplicate must not erase the surviving IO's io_table entry";
    EXPECT_EQ(room->get_size(), 1);
}

//createIO() (the API/AutoScenario path, as opposed to XML loading) owns both
//the Room and the io_table update, so a duplicate id must not be half-added:
//no growth anywhere, and the pre-existing IO is untouched.
TEST_F(ListeRoomRobustnessTest, CreateIoWithDuplicateIdIsFullyRejected)
{
    loadConfig();

    IOBase *first = io(ID_BOOL_IN);
    ASSERT_NE(first, nullptr);

    int countBefore = ListeRoom::Instance().get_io_count();
    int sizeBefore = firstRoom()->get_size();

    Params p = { { "type", "InternalInt" }, { "id", ID_BOOL_IN }, { "name", "Colliding" } };
    IOBase *result = createIO(p, firstRoom());

    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), countBefore);
    EXPECT_EQ(firstRoom()->get_size(), sizeBefore);
    EXPECT_EQ(io(ID_BOOL_IN), first);
    EXPECT_EQ(io(ID_BOOL_IN)->get_param("name"), "Bool input");
}
