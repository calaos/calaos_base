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
#ifndef S_ROOM_H
#define S_ROOM_H

#include "Calaos.h"
#include "IOBase.h"
#include <memory>
#include <type_traits>
#include <vector>

using namespace std;

namespace Calaos
{

class Room
{
protected:
    string name;
    string type;
    int hits;

    /* -------------------------------------------------------------------
     * Ownership (E4.2b)
     *
     * A Room is the ONE owner of the IOs it holds. The vector expresses it:
     * destroying the Room destroys its IOs, and no other container in the
     * tree may delete them. ListeRoom's io_table/cameraCache/audioCache are
     * non-owning indexes maintained by IOBase itself (its constructor calls
     * addIOHash(), its destructor delIOHash()).
     *
     * Every hand-out of a raw IOBase* below is therefore a NON-OWNING
     * observation, with two explicit exceptions that are ownership
     * *transfers* out of the room, and are release() (never reset()):
     *   - RemoveIO(pos, del=false), and
     *   - RemoveIOFromRoom(io).
     * ---------------------------------------------------------------- */
    vector<std::unique_ptr<IOBase>> ios;

public:
    Room(string _name, string _type, int _hits = 0);

    /* Destroys the IOs it owns, after unlinking them from the rules exactly
       like ListeRoom::deleteIO() does. Does not require the Room to be
       registered in ListeRoom (it used to: the old body asked ListeRoom to
       find this very room, which only worked while it was still in the
       rooms vector). */
    ~Room();

    string &get_name() { return name; }
    string &get_type() { return type; }

    void set_name(string &s);
    void set_type(string &s);

    int get_hits() { return hits; }

    void set_hits(int h);

    /* TAKES OWNERSHIP of p. A null p is ignored (it used to be pushed into
       the list and then dereferenced by the debug log). */
    void AddIO(IOBase *p);

    /* del = true  : destroys the IO (the room was its owner).
       del = false : TRANSFERS ownership to the caller — the IO survives,
                     detached from the room, and the caller must delete it.
                     Implemented with release(), never with a destructive
                     erase, or the pointer the caller keeps using would die
                     under its feet. Out of range is a logged no-op. */
    void RemoveIO(int i, bool del = true);

    /* TRANSFERS ownership of io out of the room without destroying it (the
       JsonApi "move an IO to another room" path re-attaches it with AddIO()
       right after). A no-op when io is not in this room. */
    void RemoveIOFromRoom(IOBase *io);

    /* Non-owning observation. nullptr when out of range. */
    IOBase *get_io(int i)
    {
        if (i < 0 || (size_t)i >= ios.size())
            return nullptr;
        return ios[i].get();
    }

    int get_size() { return (int)ios.size(); }

    bool LoadFromXml(pugi::xml_node node);
    bool SaveToXml(pugi::xml_node node);
};

}
#endif
