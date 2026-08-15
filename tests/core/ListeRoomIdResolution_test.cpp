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

//E4.2a - by-id resolution accessors of ListeRoom.
//
//First step of the ownership series (E4.2): no ownership and no lifetime is
//changed here, only the way an IO is *found* is normalized into findIO() /
//hasIO() / findIOAs<T>() / findIOByIndex() / findRoomOfIO(), with get_io()
//kept as a thin forwarder for the ~60 existing call sites.
//
//What this file pins is therefore twofold:
//  1. the contract of the new accessors (miss -> nullptr, no insertion, no
//     dereference, no room scan for the id-keyed ones), and
//  2. the invariants the next steps must not break: the IO iteration order,
//     the getCameraList()/getAudioList() signatures and contents, and the
//     delete_io(io, del) ownership-transfer semantics, which E4.2b will have
//     to reproduce with a smart pointer.

#include <list>
#include <type_traits>
#include <vector>

#include "CalaosCoreFixture.h"
#include "IntValue.h"

using namespace Calaos;
using namespace CalaosTest;

class ListeRoomIdResolutionTest: public CoreFixture
{
protected:
    //All the IOs of all the rooms, in the room-by-room order that is the
    //server-wide IO iteration order.
    static std::vector<IOBase *> iosInIterationOrder()
    {
        std::vector<IOBase *> all;
        for (int r = 0; r < ListeRoom::Instance().size(); r++)
        {
            Room *room = ListeRoom::Instance().get_room(r);
            if (!room) continue;
            for (int i = 0; i < room->get_size(); i++)
                all.push_back(room->get_io(i));
        }
        return all;
    }

    //A two-room config, to make the iteration order observable.
    void loadTwoRooms()
    {
        std::string first;
        first += internalIoXml("InternalBool", "io_e42a_r1_a", "R1 A");
        first += internalIoXml("InternalInt", "io_e42a_r1_b", "R1 B");

        std::string second;
        second += internalIoXml("InternalBool", "io_e42a_r2_a", "R2 A");
        second += internalIoXml("InternalString", "io_e42a_r2_b", "R2 B");
        second += internalIoXml("InternalInt", "io_e42a_r2_c", "R2 C");

        loadConfig(ioXmlDocument(roomXml("RoomOne", "office", first) +
                                 roomXml("RoomTwo", "cellar", second)),
                   rulesXmlDocument(""));
    }
};

/******************************************************************************
 * findIO() - the canonical by-id resolution
 ******************************************************************************/

TEST_F(ListeRoomIdResolutionTest, FindIoResolvesEveryRegisteredId)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    ASSERT_NE(lr.findIO(ID_BOOL_IN), nullptr);
    EXPECT_EQ(lr.findIO(ID_BOOL_IN)->get_param("id"), ID_BOOL_IN);
    EXPECT_EQ(lr.findIO(ID_BOOL_OUT)->get_param("name"), "Bool output");
    EXPECT_EQ(lr.findIO(ID_INT)->get_param("id"), ID_INT);
    EXPECT_EQ(lr.findIO(ID_STRING)->get_param("id"), ID_STRING);
}

//get_io(string) must stay a pure forwarder: every call site in the tree (and
//the 8 other core test binaries) still goes through it.
TEST_F(ListeRoomIdResolutionTest, GetIoStringForwardsToFindIo)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    EXPECT_EQ(lr.get_io(ID_BOOL_IN), lr.findIO(ID_BOOL_IN));
    EXPECT_EQ(lr.get_io(ID_INT), lr.findIO(ID_INT));
    EXPECT_EQ(lr.get_io("io_e42a_never_created"), lr.findIO("io_e42a_never_created"));
}

TEST_F(ListeRoomIdResolutionTest, UnknownIdResolvesToNullptr)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    EXPECT_EQ(lr.findIO("io_e42a_unknown"), nullptr);
    EXPECT_EQ(lr.findIO("IO_CORE_BOOL_IN"), nullptr) << "ids are case sensitive";
    EXPECT_EQ(lr.findIO(std::string(ID_BOOL_IN) + " "), nullptr) << "no trimming";
    EXPECT_FALSE(lr.hasIO("io_e42a_unknown"));
}

TEST_F(ListeRoomIdResolutionTest, EmptyIdResolvesToNullptr)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    //An empty id is what a JSON request without an "id" member produces. It is
    //not an identity: it must never resolve, and must never be a room scan.
    EXPECT_EQ(lr.findIO(""), nullptr);
    EXPECT_EQ(lr.findIO(std::string()), nullptr);
    EXPECT_EQ(lr.get_io(""), nullptr);
    EXPECT_FALSE(lr.hasIO(""));
    EXPECT_EQ(lr.findRoomOfIO(""), nullptr);
}

//The regression this accessor exists to make impossible: resolving a missing
//id with operator[] would default-insert a null entry and inflate the count.
TEST_F(ListeRoomIdResolutionTest, MissesNeverInsertIntoTheTable)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    const int countBefore = lr.get_io_count();

    for (int i = 0; i < 50; i++)
    {
        lr.findIO("io_e42a_miss_" + std::to_string(i));
        lr.get_io("io_e42a_miss_" + std::to_string(i));
        lr.hasIO("io_e42a_miss_" + std::to_string(i));
    }
    lr.findIO("");

    EXPECT_EQ(lr.get_io_count(), countBefore);
    EXPECT_EQ(lr.get_io_count(), 4);
}

TEST_F(ListeRoomIdResolutionTest, HasIoAgreesWithFindIo)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    EXPECT_TRUE(lr.hasIO(ID_BOOL_IN));
    EXPECT_TRUE(lr.hasIO(ID_STRING));
    EXPECT_FALSE(lr.hasIO("io_e42a_nope"));
    EXPECT_EQ(lr.hasIO(ID_BOOL_IN), lr.findIO(ID_BOOL_IN) != nullptr);
    EXPECT_EQ(lr.hasIO("io_e42a_nope"), lr.findIO("io_e42a_nope") != nullptr);
}

/******************************************************************************
 * findIOAs<T>() - the dynamic_cast pattern, guarded
 ******************************************************************************/

TEST_F(ListeRoomIdResolutionTest, TypedResolutionMatchesOrReturnsNullptr)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    //Right type: same object as the untyped resolution.
    Internal *asInternal = lr.findIOAs<Internal>(ID_INT);
    ASSERT_NE(asInternal, nullptr);
    EXPECT_EQ(static_cast<IOBase *>(asInternal), lr.findIO(ID_INT));

    //Wrong type: nullptr, and no dereference of the IO that does exist.
    EXPECT_EQ(lr.findIOAs<Scenario>(ID_INT), nullptr);

    //Unknown id: nullptr, indistinguishable from a type mismatch, on purpose.
    EXPECT_EQ(lr.findIOAs<Internal>("io_e42a_unknown"), nullptr);
    EXPECT_EQ(lr.findIOAs<Scenario>(""), nullptr);
}

/******************************************************************************
 * findIOByIndex() - positional resolution, iteration order
 ******************************************************************************/

//THE invariant of this ticket: the positional order is rooms in declaration
//order, then IOs in their in-room order. It is the order rule evaluation sees,
//so a reordering here is a silent behavior change.
TEST_F(ListeRoomIdResolutionTest, IndexResolutionKeepsTheRoomByRoomIterationOrder)
{
    loadTwoRooms();

    ListeRoom &lr = ListeRoom::Instance();
    ASSERT_EQ(lr.size(), 2);

    std::vector<IOBase *> expected = iosInIterationOrder();
    ASSERT_EQ(expected.size(), 5u);

    for (size_t i = 0; i < expected.size(); i++)
    {
        EXPECT_EQ(lr.findIOByIndex((int)i), expected[i]) << "at index " << i;
        EXPECT_EQ(lr.get_io((int)i), expected[i]) << "get_io(int) forwarder, index " << i;
    }

    //Spelled out, so that a reordering fails on a readable assertion too.
    EXPECT_EQ(lr.findIOByIndex(0)->get_param("id"), "io_e42a_r1_a");
    EXPECT_EQ(lr.findIOByIndex(1)->get_param("id"), "io_e42a_r1_b");
    EXPECT_EQ(lr.findIOByIndex(2)->get_param("id"), "io_e42a_r2_a");
    EXPECT_EQ(lr.findIOByIndex(3)->get_param("id"), "io_e42a_r2_b");
    EXPECT_EQ(lr.findIOByIndex(4)->get_param("id"), "io_e42a_r2_c");
}

TEST_F(ListeRoomIdResolutionTest, IndexResolutionOutOfRangeIsNullptr)
{
    loadTwoRooms();

    ListeRoom &lr = ListeRoom::Instance();

    EXPECT_EQ(lr.findIOByIndex(-1), nullptr);
    EXPECT_EQ(lr.findIOByIndex(5), nullptr);
    EXPECT_EQ(lr.findIOByIndex(10000), nullptr);
    EXPECT_EQ(lr.get_io(-1), nullptr);
    EXPECT_EQ(lr.get_io(5), nullptr);
}

/******************************************************************************
 * findRoomOfIO() - guarded composition
 ******************************************************************************/

TEST_F(ListeRoomIdResolutionTest, RoomOfIoResolvesThroughTheIdOrFailsCleanly)
{
    loadTwoRooms();

    ListeRoom &lr = ListeRoom::Instance();

    Room *r1 = findRoom("RoomOne");
    Room *r2 = findRoom("RoomTwo");
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);

    EXPECT_EQ(lr.findRoomOfIO("io_e42a_r1_b"), r1);
    EXPECT_EQ(lr.findRoomOfIO("io_e42a_r2_c"), r2);
    EXPECT_EQ(lr.findRoomOfIO("io_e42a_r1_b"), lr.getRoomByIO(lr.findIO("io_e42a_r1_b")));

    EXPECT_EQ(lr.findRoomOfIO("io_e42a_unknown"), nullptr);
    EXPECT_EQ(lr.findRoomOfIO(""), nullptr);

    //A null needle must not be answered by a room, whatever the rooms hold.
    EXPECT_EQ(lr.getRoomByIO(nullptr), nullptr);
}

/******************************************************************************
 * Duplicate ids: resolution is by id only, never a room scan
 ******************************************************************************/

//An IO rejected by addIOHash() stays in its Room but has no io_table entry:
//the by-id accessors must keep returning the first, authoritative one, and
//must not fall back on scanning the rooms to "find" the duplicate.
TEST_F(ListeRoomIdResolutionTest, CollidingIdAlwaysResolvesToTheAuthoritativeIo)
{
    std::string ios;
    ios += internalIoXml("InternalBool", "io_e42a_dup", "First");
    ios += internalIoXml("InternalInt", "io_e42a_dup", "Second");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    ListeRoom &lr = ListeRoom::Instance();
    ASSERT_EQ(lr.get_io_count(), 1);

    IOBase *survivor = lr.findIO("io_e42a_dup");
    ASSERT_NE(survivor, nullptr);
    EXPECT_EQ(survivor->get_param("name"), "First");
    EXPECT_TRUE(lr.hasIO("io_e42a_dup"));

    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    ASSERT_EQ(room->get_size(), 2) << "the rejected duplicate still lives in the room";

    IOBase *duplicate = room->get_io(0) == survivor ? room->get_io(1) : room->get_io(0);
    ASSERT_NE(duplicate, nullptr);
    ASSERT_NE(duplicate, survivor);

    //Both are reachable positionally (they are both in a room), only one is
    //reachable by id. That asymmetry is the point.
    EXPECT_NE(lr.findIOByIndex(0), lr.findIOByIndex(1));
    EXPECT_EQ(lr.findIO(duplicate->get_param("id")), survivor);

    //Destroying the duplicate must not take the survivor's entry with it.
    ASSERT_TRUE(deleteIO(duplicate));
    EXPECT_EQ(lr.findIO("io_e42a_dup"), survivor);
    EXPECT_EQ(lr.get_io_count(), 1);
}

//createIO() resolves the freshly built IO by id to detect a rejection: the
//colliding IO must be destroyed, and the pre-existing one still resolvable.
TEST_F(ListeRoomIdResolutionTest, CreateIoUsesResolutionToRejectACollision)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    IOBase *first = lr.findIO(ID_BOOL_IN);
    ASSERT_NE(first, nullptr);

    Params p = { { "type", "InternalInt" }, { "id", ID_BOOL_IN }, { "name", "Colliding" } };
    EXPECT_EQ(createIO(p, firstRoom()), nullptr);

    EXPECT_EQ(lr.findIO(ID_BOOL_IN), first);
    EXPECT_EQ(lr.findIO(ID_BOOL_IN)->get_param("name"), "Bool input");
    EXPECT_EQ(lr.get_io_count(), 4);
}

/******************************************************************************
 * Invariants the ownership steps must not break
 ******************************************************************************/

//getCameraList()/getAudioList() feed JsonApi payloads: both the signature
//(a std::list of raw IOBase*, returned by value) and the contents are part of
//the wire contract. Changing the element type would change the payloads.
static_assert(std::is_same<decltype(std::declval<ListeRoom &>().getCameraList()),
                           std::list<IOBase *>>::value,
              "getCameraList() must keep returning list<IOBase*> by value (JsonApi payload)");
static_assert(std::is_same<decltype(std::declval<ListeRoom &>().getAudioList()),
                           std::list<IOBase *>>::value,
              "getAudioList() must keep returning list<IOBase*> by value (JsonApi payload)");

TEST_F(ListeRoomIdResolutionTest, ResolutionDoesNotDisturbTheCameraAndAudioCaches)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    //No hardware IO is linked into the core tests, so the caches are fed by
    //re-registering an internal IO under a camera/audio gui_type. That is the
    //very path addIOHash() uses when a real IPCam/AudioPlayer is built.
    IOBase *cam = lr.findIO(ID_BOOL_IN);
    IOBase *audio = lr.findIO(ID_BOOL_OUT);
    ASSERT_NE(cam, nullptr);
    ASSERT_NE(audio, nullptr);

    EXPECT_TRUE(lr.getCameraList().empty());
    EXPECT_TRUE(lr.getAudioList().empty());

    cam->set_param("gui_type", "camera");
    audio->set_param("gui_type", "audio_player");
    lr.addIOHash(cam);
    lr.addIOHash(audio);

    ASSERT_EQ(lr.getCameraList().size(), 1u);
    ASSERT_EQ(lr.getAudioList().size(), 1u);
    EXPECT_EQ(lr.getCameraList().front(), cam);
    EXPECT_EQ(lr.getAudioList().front(), audio);

    //Resolutions, hits and misses alike, are pure reads.
    for (int i = 0; i < 10; i++)
    {
        lr.findIO(ID_BOOL_IN);
        lr.findIO("io_e42a_miss");
        lr.findIO("");
        lr.findIOByIndex(i);
        lr.findRoomOfIO("io_e42a_miss");
        lr.hasIO(ID_BOOL_OUT);
    }

    EXPECT_EQ(lr.getCameraList().size(), 1u);
    EXPECT_EQ(lr.getAudioList().size(), 1u);
    EXPECT_EQ(lr.getCameraList().front(), cam);
    EXPECT_EQ(lr.getAudioList().front(), audio);
    EXPECT_EQ(lr.get_io_count(), 4);
}

//delete_io(io, del) is deliberately untouched by E4.2a. del=false is an
//ownership *transfer* out of the Room (the object survives, and stays
//registered until someone deletes it), del=true destroys it. E4.2b has to
//reproduce exactly this with release() vs reset().
TEST_F(ListeRoomIdResolutionTest, DeleteIoOwnershipTransferSemanticsAreUnchanged)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    IOBase *detached = lr.findIO(ID_STRING);
    ASSERT_NE(detached, nullptr);
    const int roomSizeBefore = room->get_size();

    //del=false: out of the room, but alive and still resolvable by id.
    ASSERT_TRUE(lr.delete_io(detached, false));
    EXPECT_EQ(room->get_size(), roomSizeBefore - 1);
    EXPECT_EQ(lr.findIO(ID_STRING), detached) << "del=false must not destroy the IO";
    EXPECT_EQ(lr.getRoomByIO(detached), nullptr) << "and must detach it from its room";

    //The caller now owns it: destroying it unregisters it.
    delete detached;
    EXPECT_EQ(lr.findIO(ID_STRING), nullptr);
    EXPECT_EQ(lr.get_io_count(), 3);

    //del=true: destroyed on the spot, and unresolvable right away.
    IOBase *destroyed = lr.findIO(ID_INT);
    ASSERT_NE(destroyed, nullptr);
    ASSERT_TRUE(lr.delete_io(destroyed, true));
    EXPECT_EQ(lr.findIO(ID_INT), nullptr);
    EXPECT_EQ(lr.get_io_count(), 2);

    //An IO that is in no room at all is simply not deleted.
    EXPECT_FALSE(lr.delete_io(nullptr, true));
}

/******************************************************************************
 * Null and out of range guards (T2.18 style: never an unchecked dereference)
 ******************************************************************************/

TEST_F(ListeRoomIdResolutionTest, DeleteIoWithNoIoIsAPlainFalse)
{
    loadConfig();

    //Callers pass a resolution result straight in; a miss must not be a
    //dereference of nullptr inside deleteIO().
    EXPECT_FALSE(ListeRoom::Instance().deleteIO(nullptr));
    EXPECT_FALSE(ListeRoom::Instance().deleteIO(nullptr, true));
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 4);
}

TEST_F(ListeRoomIdResolutionTest, RoomIndexAccessorsAreBoundsChecked)
{
    loadTwoRooms();

    ListeRoom &lr = ListeRoom::Instance();
    ASSERT_EQ(lr.size(), 2);

    EXPECT_NE(lr.get_room(0), nullptr);
    EXPECT_NE(lr.get_room(1), nullptr);
    EXPECT_EQ(lr.get_room(2), nullptr);
    EXPECT_EQ(lr.get_room(-1), nullptr);

    const ListeRoom &clr = lr;
    EXPECT_EQ(clr[0], lr.get_room(0));
    EXPECT_EQ(clr[2], nullptr);
    EXPECT_EQ(clr[-1], nullptr);

    //Remove() used to walk the iterator past end() and delete out of bounds.
    lr.Remove(7);
    lr.Remove(-1);
    EXPECT_EQ(lr.size(), 2) << "an out of range Remove() must be a no-op";
    EXPECT_NE(lr.findIO("io_e42a_r2_c"), nullptr) << "no room was destroyed";
}

//get_chauffage_var() scans every IO of every room: the scan must survive a
//room holding nothing, and keep answering by gui_type as before.
TEST_F(ListeRoomIdResolutionTest, ChauffageLookupScansEveryRoomWithoutDereferencingBlindly)
{
    loadTwoRooms();

    ListeRoom &lr = ListeRoom::Instance();
    addRoom("EmptyRoom", "misc");

    std::string chauffId = "chauff_e42a";
    EXPECT_EQ(lr.get_chauffage_var(chauffId, CONSIGNE), nullptr);

    IOBase *consigne = lr.findIO("io_e42a_r2_c");
    ASSERT_NE(consigne, nullptr);
    consigne->set_param("chauffage_id", chauffId);
    consigne->set_param("gui_type", "var_int");

    EXPECT_EQ(lr.get_chauffage_var(chauffId, CONSIGNE), consigne);
    EXPECT_EQ(lr.get_chauffage_var(chauffId, ACTIVE), nullptr);
    EXPECT_EQ(lr.get_chauffage_var(chauffId, PLAGE_HORAIRE), nullptr);
}
