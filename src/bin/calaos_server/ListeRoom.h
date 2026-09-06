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
#ifndef S_ListeRoom_H
#define S_ListeRoom_H

#include <memory>
#include <vector>

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
    /* -------------------------------------------------------------------
     * Ownership (E4.2b)
     *
     * ListeRoom owns the Rooms, a Room owns its IOs. Nothing else in the
     * tree may delete an IOBase, and there is no `delete` left here.
     *
     * io_table / cameraCache / audioCache are NON-OWNING indexes over IOs
     * owned by the rooms. They are not filled by this class: IOBase's
     * constructor registers itself through addIOHash() and its destructor
     * unregisters through delIOHash(), so an entry can never outlive the
     * object it points at, whichever way that object is destroyed (room
     * destruction, delete_io(), or a `delete` by the caller an IO was
     * transferred to).
     * ---------------------------------------------------------------- */
    std::vector<std::unique_ptr<Room>> rooms;
    unordered_map<string, IOBase *> io_table;

    list<IOBase *> cameraCache;
    list<IOBase *> audioCache;

    list<Scenario *> auto_scenario_cache;

    ListeRoom();

    /* True when io_table currently holds this exact IO under its own id,
       i.e. addIOHash() accepted it. Deliberately a raw io_table lookup and
       not findIO(): it must also answer for the degenerate "" key that a
       malformed io.xml can produce, which findIO() refuses to resolve. */
    bool isHashRegistered(IOBase *io) const;

public:
    //singleton
    static ListeRoom &Instance();

    ~ListeRoom();

    /* TAKES OWNERSHIP of p (a null p is ignored). */
    void Add(Room *p);
    /* Destroys the room at index i and every IO it owns. Out of range is a
       logged no-op. */
    void Remove(int i);
    Room *get_room(int i);
    Room *operator[] (int i) const;

    /* -------------------------------------------------------------------
     * Resolution accessors (E4.2a)
     *
     * Single entry point for every "give me the IO whose id is X" question.
     * The contract, which the rest of the tree may rely on:
     *   - the returned pointer is NON-OWNING. The IO is owned by its Room;
     *     the pointer is only valid until that IO is destroyed. Holding it
     *     across a delete/reload is what E4.2 is about, and is still the
     *     caller's problem at this stage.
     *   - an unknown id yields nullptr, explicitly. The lookup never inserts
     *     into io_table (no operator[] on a missing key) and never
     *     dereferences what it found.
     *   - an empty id is not an identity: it always resolves to nullptr,
     *     even in the degenerate case of a malformed io.xml having pushed an
     *     id-less IO into io_table under the "" key. That accidental entry
     *     stays correctly book-kept by addIOHash()/delIOHash(), it is simply
     *     not addressable.
     *   - resolution is by id only: it never scans the rooms, so an IO that
     *     addIOHash() rejected as a duplicate is invisible here (by design,
     *     the first registered IO stays authoritative).
     * ---------------------------------------------------------------- */
    IOBase *findIO(const std::string &id) const;

    /* Presence test that never hands out a pointer, for call sites that only
       need to know whether an id is taken. */
    bool hasIO(const std::string &id) const;

    /* findIO() + dynamic_cast, i.e. the pattern spelled out by hand at ~13
       call sites (JsonApi, AutoScenario, Rules/…). nullptr both when the id
       is unknown and when the IO exists but is not a T: a miss is never
       distinguishable from a type mismatch, and neither is dereferenced. */
    template<typename T>
    T *findIOAs(const std::string &id) const { return dynamic_cast<T *>(findIO(id)); }

    /* Positional resolution over the rooms, in room order then in-room order.
       That order is the IO iteration order of the whole server and MUST NOT
       change. Out of range yields nullptr. */
    IOBase *findIOByIndex(int index);

    /* Room owning the IO with this id, or nullptr if either is unknown.
       Composition of findIO() and getRoomByIO(), with no deref in between. */
    Room *findRoomOfIO(const std::string &id);

    /* Historical names, kept so the ~60 existing call sites are untouched.
       Thin forwarders to the accessors above. */
    IOBase *get_io(std::string id);
    IOBase *get_io(int i);

    /* Removes io from the room that owns it.
       del = true  : the IO is destroyed.
       del = false : ownership is TRANSFERRED to the caller — the IO survives
                     the call, stays registered in io_table (so it is still
                     resolvable by id) and the caller must delete it. It is a
                     release(), never a reset().
       Returns false when io is in no room at all, without destroying
       anything: calling it twice on the same IO is safe, the second call is
       a no-op. */
    bool delete_io(IOBase *io, bool del = true);

    /* The rule-side half of deleteIO(): deals with the rules that use this IO
       and takes it out of the polling list, without touching its ownership.
       Public because Room's destructor has to run it for each IO it is about
       to destroy. `modify` = true keeps the rules untouched (an IO being
       edited). `policy` is handed straight to ListeRule::RemoveRule(): the
       default DISABLES the rules instead of destroying them (T3.18).
       A null IO is a no-op. */
    void detachIOFromRules(IOBase *io, bool modify = false,
                           RuleDetachPolicy policy = RuleDetachPolicy::Disable);

    int get_io_count(); //total IO count for all rooms

    int size() { return rooms.size(); }

    IOBase *get_chauffage_var(std::string &chauff_id, ChauffType type);

    /* Copies of the non-owning caches. Both the element type and the return
       by value are part of the JsonApi payload contract (a static_assert in
       tests/core/ListeRoomIdResolution_test.cpp pins them): the ownership
       move must NOT turn these into smart pointers. */
    list<IOBase *> getCameraList() { return cameraCache; }
    list<IOBase *> getAudioList() { return audioCache; }

    //Auto scenarios

    void addScenarioCache(Scenario *sc);
    void delScenarioCache(Scenario *sc);

    /* Told by Room just before it destroys one of its IOs, or destroys itself.
       An AutoScenario holds raw pointers to the five IOs that drive it and to
       the room they live in, and refreshBrokenScenarios() below reads those
       IOs back at the end of EVERY deletion - a scenario sharing a machinery
       IO with the one being deleted would be read through a freed pointer.
       Sweeps, so no registration has to be kept in step with them. */
    void forgetIOInAutoScenarios(IOBase *io);
    void forgetRoomInAutoScenarios(Room *room);
    list<Scenario *> getAutoScenarios();
    void checkAutoScenario();

    /* A whole configuration uploaded through `config put` can drop a scenario
     * or shorten one, and the server has no say in it: refusing is ruled out
     * (deleting a scenario from calaos_installer has to keep working), so all
     * it owes the user is to SAY what went missing. It cannot say it during
     * the upload: a successful put restarts the server as soon as the reply
     * is out, and the alert channel is deferred by 30s so it fires with the
     * event loop up. The finding therefore has to cross the restart, which is
     * what the snapshot below is for.
     */

    //One scenario as the server knew it, i.e. what the diff compares.
    struct KnownAutoScenario
    {
        string uid;
        string name;
        size_t stepCount = 0;
    };

    vector<KnownAutoScenario> knownAutoScenarios();

    /* Record the scenarios currently loaded, next to the backup `config put`
       takes just before overwriting the files. */
    void snapshotAutoScenariosBeforeUpload();

    /* Read that snapshot back, CONSUME it, and raise one configuration alert
       naming every scenario the upload dropped or shortened. Consuming is
       what keeps a loss from being re-announced at every boot afterwards.
       Does nothing at all when no upload preceded this startup. */
    void reportAutoScenariosLostByUpload();

    /* The comparison itself, kept pure so it can be read on its own: one
       human readable line per loss, none when nothing was lost. */
    static vector<string> diffLostAutoScenarios(const vector<KnownAutoScenario> &before,
                                                const vector<KnownAutoScenario> &after);

    Room * searchRoomByNameAndType(string name,string type);

    Room *getRoomByIO(IOBase *o);

    /* detachIOFromRules() + delete_io(io): the full "the user deleted this
       IO" path used by the JSON API. The IO is destroyed.
       `policy` is handed to detachIOFromRules(); with the default (Disable) the
       detection pass below runs once the IO is really gone. */
    bool deleteIO(IOBase *io, bool modify = false,
                  RuleDetachPolicy policy = RuleDetachPolicy::Disable);

    /* T3.18. Walk the auto scenarios and DISABLE the broken ones: for each one
       whose AutoScenario::isBroken() answers true, set the persisted
       `disabled_missing_io` flag and bring a running scenario to a clean stop.

       IT ONLY EVER SETS. Never clearing is not an oversight, it is the user's
       decision: the disabling is sticky until an explicit re-enable, so putting
       the IO back does not restart the scenario on its own. A setXxx(false)
       slipped into this pass would make the whole ticket pointless, and it
       would only show up after a reboot.

       Two callers, and two only: deleteIO() (hot path, once the IO is gone) and
       checkAutoScenario() (startup), both of which already call SaveConfigIO()
       right after - no new save is added anywhere. */
    void refreshBrokenScenarios();

    /* Builds an IO through IOFactory and hands its ownership to `room`.
       Returns a non-owning pointer to it, or nullptr when the creation
       failed or when the id was rejected as a duplicate (in which case the
       half-built IO is destroyed here, never left attached to the room). */
    IOBase* createIO(Params param, Room *room);

    void addIOHash(IOBase *io);
    void delIOHash(IOBase *io);
};

}

#endif
