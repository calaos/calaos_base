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

//E4.2b - explicit and unique ownership of the IOs (step 2/6 of the E4.2
//series, after E4.2a's by-id resolution accessors).
//
//The model this file pins:
//  - a Room is the ONE owner of its IOs (vector<unique_ptr<IOBase>>), so
//    destroying a Room destroys them, whether or not it is registered in
//    ListeRoom;
//  - ListeRoom's io_table / cameraCache / audioCache are NON-OWNING indexes,
//    kept up to date by IOBase itself (addIOHash/delIOHash), so an entry can
//    never outlive its object, whichever path destroyed it. That property is
//    what lets these tests prove "the IO was really destroyed" (its id stops
//    resolving) and "the IO is still alive" (its id still resolves) without a
//    leak checker;
//  - delete_io(io, del=false) and RemoveIOFromRoom() are ownership TRANSFERS
//    out of the room: the object survives and the receiver must delete it.
//    They are release(), never a destructive erase;
//  - removing an IO twice is a no-op, never a double free.
//
//The invariants of E4.2a (iteration order, getCameraList()/getAudioList()
//type and contents) are pinned by core/ListeRoomIdResolution_test.cpp, which
//E4.2b leaves untouched.

#include <memory>

#include "CalaosCoreFixture.h"
#include "IntValue.h"
#include "ListeRoom.h"
#include "Room.h"

using namespace Calaos;
using namespace CalaosTest;

class ListeRoomOwnershipTest: public CoreFixture
{
protected:
    static int roomIndexOf(Room *room)
    {
        for (int i = 0; i < ListeRoom::Instance().size(); i++)
            if (ListeRoom::Instance().get_room(i) == room)
                return i;
        return -1;
    }
};

/******************************************************************************
 * delete_io(io, del=false): ownership transfer
 ******************************************************************************/

//TRAP of the ticket: del=false is not "remove quietly", it hands the object
//over. A reset()/erase() of a live unique_ptr here would destroy the IO under
//the feet of the caller that is still holding the pointer.
TEST_F(ListeRoomOwnershipTest, DelFalseTransfersTheIoAndLeavesItUsable)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    IOBase *detached = lr.findIO(ID_STRING);
    ASSERT_NE(detached, nullptr);
    const int roomSizeBefore = room->get_size();
    const int countBefore = lr.get_io_count();

    ASSERT_TRUE(lr.delete_io(detached, false));

    //Out of the room...
    EXPECT_EQ(room->get_size(), roomSizeBefore - 1);
    EXPECT_EQ(lr.getRoomByIO(detached), nullptr);

    //...but alive, and still an IO: the vtable answers, the parameters answer,
    //and writing one back is readable again.
    ASSERT_NE(dynamic_cast<Internal *>(detached), nullptr);
    EXPECT_EQ(detached->get_param("id"), ID_STRING);
    detached->set_param("name", "transferred");
    EXPECT_EQ(detached->get_param("name"), "transferred");

    //Still registered in the non-owning index, so still resolvable by id.
    EXPECT_EQ(lr.findIO(ID_STRING), detached);
    EXPECT_TRUE(lr.hasIO(ID_STRING));
    EXPECT_EQ(lr.get_io_count(), countBefore);

    //The caller is the owner now: its delete is the one that destroys it, and
    //unregisters it from the index.
    delete detached;
    EXPECT_EQ(lr.findIO(ID_STRING), nullptr);
    EXPECT_EQ(lr.get_io_count(), countBefore - 1);
}

//The transfer must survive the death of the room the IO came from. If
//RemoveIO(pos, false) erased a live unique_ptr instead of releasing it, this
//would already have crashed above; if it merely forgot to detach, the room
//destruction below would free the IO a second time.
TEST_F(ListeRoomOwnershipTest, TransferredIoSurvivesTheDestructionOfItsFormerRoom)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    IOBase *detached = lr.findIO(ID_STRING);
    ASSERT_NE(detached, nullptr);
    ASSERT_TRUE(lr.delete_io(detached, false));

    lr.Remove(roomIndexOf(room));
    EXPECT_EQ(lr.size(), 0);

    EXPECT_EQ(lr.findIO(ID_STRING), detached) << "the transferred IO must not die with its old room";
    EXPECT_EQ(detached->get_param("id"), ID_STRING);
    EXPECT_EQ(lr.get_io_count(), 1) << "every other IO of the room is gone";

    delete detached;
    EXPECT_EQ(lr.get_io_count(), 0);
}

/******************************************************************************
 * Double removal
 ******************************************************************************/

//Once an IO has left its room, nobody owns it but the caller: every further
//removal attempt must be a plain false that destroys nothing, otherwise the
//caller's own delete would be a double free.
TEST_F(ListeRoomOwnershipTest, RemovingAnIoTwiceIsANoOpNotADoubleFree)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();

    IOBase *detached = lr.findIO(ID_STRING);
    ASSERT_NE(detached, nullptr);

    ASSERT_TRUE(lr.delete_io(detached, false));

    EXPECT_FALSE(lr.delete_io(detached, false)) << "no room owns it anymore";
    EXPECT_FALSE(lr.delete_io(detached, true)) << "and del=true must not destroy it either";
    EXPECT_FALSE(lr.deleteIO(detached)) << "same through the JSON API entry point";

    //Still perfectly alive after the three failed removals.
    EXPECT_EQ(lr.findIO(ID_STRING), detached);
    EXPECT_EQ(detached->get_param("id"), ID_STRING);

    delete detached;
    EXPECT_EQ(lr.findIO(ID_STRING), nullptr);
}

//The destructive path: after delete_io(io, true) the object is gone, and the
//id it held stops resolving immediately. Re-deleting by id is then impossible
//by construction, which is the point of resolving by id instead of holding a
//raw pointer.
TEST_F(ListeRoomOwnershipTest, DeleteIoDestroysAndUnregistersInOneStep)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    const int roomSizeBefore = room->get_size();
    const int countBefore = lr.get_io_count();

    ASSERT_TRUE(lr.delete_io(lr.findIO(ID_STRING), true));

    EXPECT_EQ(lr.findIO(ID_STRING), nullptr);
    EXPECT_FALSE(lr.hasIO(ID_STRING));
    EXPECT_EQ(lr.get_io_count(), countBefore - 1);
    EXPECT_EQ(room->get_size(), roomSizeBefore - 1);

    //A second attempt has nothing left to resolve, so nothing to delete.
    EXPECT_FALSE(lr.delete_io(lr.findIO(ID_STRING), true));
    EXPECT_FALSE(lr.deleteIO(lr.findIO(ID_STRING)));
    EXPECT_EQ(lr.get_io_count(), countBefore - 1);
}

/******************************************************************************
 * A Room owns its IOs: destroying it destroys them, and leaks none
 ******************************************************************************/

TEST_F(ListeRoomOwnershipTest, DestroyingARoomDestroysEveryIoItOwns)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    const int countBefore = lr.get_io_count();

    Room *doomed = addRoom("DoomedRoom", "misc");
    ASSERT_NE(doomed, nullptr);
    ASSERT_NE(createInternalIO("InternalBool", "io_e42b_doomed_a", "A", doomed), nullptr);
    ASSERT_NE(createInternalIO("InternalInt", "io_e42b_doomed_b", "B", doomed), nullptr);
    ASSERT_NE(createInternalIO("InternalString", "io_e42b_doomed_c", "C", doomed), nullptr);
    ASSERT_EQ(doomed->get_size(), 3);
    ASSERT_EQ(lr.get_io_count(), countBefore + 3);

    lr.Remove(roomIndexOf(doomed));

    //Every id stopped resolving: ~IOBase ran for all three, so none leaked.
    EXPECT_EQ(lr.findIO("io_e42b_doomed_a"), nullptr);
    EXPECT_EQ(lr.findIO("io_e42b_doomed_b"), nullptr);
    EXPECT_EQ(lr.findIO("io_e42b_doomed_c"), nullptr);
    EXPECT_EQ(lr.get_io_count(), countBefore);
    EXPECT_EQ(lr.size(), 1);
}

//A Room that ListeRoom never heard of still owns and destroys its IOs. Before
//E4.2b the destructor asked ListeRoom to find this very room in order to
//delete them: for an unregistered room the lookup failed, the IO list never
//shrank, and the `while` loop spun forever.
TEST_F(ListeRoomOwnershipTest, AnUnregisteredRoomStillDestroysItsIos)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    const int countBefore = lr.get_io_count();
    const int roomsBefore = lr.size();

    //Deliberately NOT handed to ListeRoom::Add(): this scope owns it.
    std::unique_ptr<Room> orphan(new Room("OrphanRoom", "misc", 0));
    ASSERT_EQ(lr.size(), roomsBefore);

    Params p = { { "type", "InternalBool" }, { "id", "io_e42b_orphan" }, { "name", "Orphan" } };
    IOBase *io = lr.createIO(p, orphan.get());
    ASSERT_NE(io, nullptr);
    ASSERT_EQ(orphan->get_size(), 1);
    ASSERT_EQ(lr.findIO("io_e42b_orphan"), io);
    ASSERT_EQ(lr.get_io_count(), countBefore + 1);

    orphan.reset();

    EXPECT_EQ(lr.findIO("io_e42b_orphan"), nullptr);
    EXPECT_EQ(lr.get_io_count(), countBefore);
}

//The caches are indexes, not owners: they must be emptied by the destruction
//of the IOs they point at, not keep dangling entries that getCameraList()
//would then hand to the JsonApi payload builder.
TEST_F(ListeRoomOwnershipTest, DestroyingARoomEmptiesTheNonOwningCaches)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    //Same trick as E4.2a: no hardware IO is linked in the core tests, so the
    //caches are fed by re-registering internal IOs under a camera/audio
    //gui_type, which is the very path a real IPCam/AudioPlayer takes.
    IOBase *cam = lr.findIO(ID_BOOL_IN);
    IOBase *audio = lr.findIO(ID_BOOL_OUT);
    ASSERT_NE(cam, nullptr);
    ASSERT_NE(audio, nullptr);
    cam->set_param("gui_type", "camera");
    audio->set_param("gui_type", "audio_player");
    lr.addIOHash(cam);
    lr.addIOHash(audio);
    ASSERT_EQ(lr.getCameraList().size(), 1u);
    ASSERT_EQ(lr.getAudioList().size(), 1u);

    lr.Remove(roomIndexOf(room));

    EXPECT_TRUE(lr.getCameraList().empty());
    EXPECT_TRUE(lr.getAudioList().empty());
    EXPECT_EQ(lr.get_io_count(), 0);
}

/******************************************************************************
 * RemoveIOFromRoom(): the other ownership transfer
 ******************************************************************************/

//Moving an IO from one room to another (the JsonApi "change the room of a
//scenario" path) must leave exactly one owner: the old room must not free it,
//the new one must.
TEST_F(ListeRoomOwnershipTest, MovingAnIoBetweenRoomsKeepsExactlyOneOwner)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *first = firstRoom();
    Room *second = addRoom("SecondRoom", "misc");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    IOBase *moved = lr.findIO(ID_STRING);
    ASSERT_NE(moved, nullptr);
    const int firstSizeBefore = first->get_size();

    first->RemoveIOFromRoom(moved);
    second->AddIO(moved);

    EXPECT_EQ(first->get_size(), firstSizeBefore - 1);
    EXPECT_EQ(second->get_size(), 1);
    EXPECT_EQ(lr.getRoomByIO(moved), second);
    EXPECT_EQ(lr.findIO(ID_STRING), moved);

    //The old room no longer owns it: destroying it leaves the IO alive.
    lr.Remove(roomIndexOf(first));
    EXPECT_EQ(lr.findIO(ID_STRING), moved);
    EXPECT_EQ(lr.get_io_count(), 1);

    //The new room does: destroying it destroys the IO, exactly once.
    lr.Remove(roomIndexOf(second));
    EXPECT_EQ(lr.findIO(ID_STRING), nullptr);
    EXPECT_EQ(lr.get_io_count(), 0);
    EXPECT_EQ(lr.size(), 0);
}

//A no-op when the IO is not in that room: nothing is released, nothing is
//destroyed, and the real owner keeps it.
TEST_F(ListeRoomOwnershipTest, RemoveIoFromTheWrongRoomChangesNothing)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *first = firstRoom();
    Room *second = addRoom("SecondRoom", "misc");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    IOBase *io = lr.findIO(ID_STRING);
    ASSERT_NE(io, nullptr);
    const int firstSizeBefore = first->get_size();

    second->RemoveIOFromRoom(io);

    EXPECT_EQ(first->get_size(), firstSizeBefore);
    EXPECT_EQ(second->get_size(), 0);
    EXPECT_EQ(lr.getRoomByIO(io), first);
    EXPECT_EQ(lr.findIO(ID_STRING), io);
}

/******************************************************************************
 * createIO(): no half-added IO (E4.2a carry-over)
 ******************************************************************************/

//The carry-over fixed here: createIO() now asks io_table whether addIOHash()
//accepted the IO, instead of asking findIO() whether it can be resolved. The
//E4.2a `!id.empty()` special case is gone, and with it the only state in which
//an IO was attached to a room while absent from io_table.
TEST_F(ListeRoomOwnershipTest, CreateIoKeepsTheFirstIdLessIoAndDiscardsTheSecond)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    const int countBefore = lr.get_io_count();
    const int sizeBefore = room->get_size();

    //An explicitly empty id: accepted by addIOHash() under the "" key, so the
    //IO is kept, exactly as it was before the E4.2a rewrite.
    Params first = { { "type", "InternalBool" }, { "id", "" }, { "name", "First idless" } };
    IOBase *idless = lr.createIO(first, room);
    ASSERT_NE(idless, nullptr);
    EXPECT_EQ(room->get_size(), sizeBefore + 1);
    EXPECT_EQ(lr.get_io_count(), countBefore + 1);
    //Registered, but not addressable: an empty id is not an identity.
    EXPECT_EQ(lr.findIO(""), nullptr);
    EXPECT_EQ(lr.getRoomByIO(idless), room);

    //A second one collides on the "" key: addIOHash() rejects it, so createIO()
    //must destroy it rather than leave it in the room and out of io_table.
    Params second = { { "type", "InternalInt" }, { "id", "" }, { "name", "Second idless" } };
    EXPECT_EQ(lr.createIO(second, room), nullptr);
    EXPECT_EQ(room->get_size(), sizeBefore + 1) << "no half-added IO left in the room";
    EXPECT_EQ(lr.get_io_count(), countBefore + 1);
}

//The ordinary collision, unchanged: nothing is added anywhere and the
//pre-existing IO is untouched.
TEST_F(ListeRoomOwnershipTest, CreateIoWithADuplicateIdAddsNothing)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);

    IOBase *existing = lr.findIO(ID_BOOL_IN);
    ASSERT_NE(existing, nullptr);
    const int countBefore = lr.get_io_count();
    const int sizeBefore = room->get_size();

    Params p = { { "type", "InternalInt" }, { "id", ID_BOOL_IN }, { "name", "Colliding" } };
    EXPECT_EQ(lr.createIO(p, room), nullptr);

    EXPECT_EQ(lr.findIO(ID_BOOL_IN), existing);
    EXPECT_EQ(lr.findIO(ID_BOOL_IN)->get_param("name"), "Bool input");
    EXPECT_EQ(lr.get_io_count(), countBefore);
    EXPECT_EQ(room->get_size(), sizeBefore);
}

/******************************************************************************
 * Null and out of range: the ownership containers are guarded too
 ******************************************************************************/

TEST_F(ListeRoomOwnershipTest, NullAndOutOfRangeOwnershipOperationsAreNoOps)
{
    loadConfig();

    ListeRoom &lr = ListeRoom::Instance();
    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    const int sizeBefore = room->get_size();
    const int countBefore = lr.get_io_count();

    //A null IO is owned by nobody.
    EXPECT_FALSE(lr.delete_io(nullptr, true));
    EXPECT_FALSE(lr.delete_io(nullptr, false));
    room->RemoveIOFromRoom(nullptr);

    //A null room is not adopted (it used to be pushed and then dereferenced).
    lr.Add(nullptr);
    room->AddIO(nullptr);

    //Out of range removals touch nothing.
    room->RemoveIO(-1, true);
    room->RemoveIO(sizeBefore, true);
    room->RemoveIO(sizeBefore + 10, false);
    lr.Remove(-1);
    lr.Remove(lr.size() + 5);

    EXPECT_EQ(room->get_size(), sizeBefore);
    EXPECT_EQ(lr.get_io_count(), countBefore);
    EXPECT_EQ(lr.size(), 1);
    EXPECT_EQ(room->get_io(-1), nullptr);
    EXPECT_EQ(room->get_io(sizeBefore), nullptr);
    EXPECT_NE(room->get_io(0), nullptr);
}
