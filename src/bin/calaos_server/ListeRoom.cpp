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
#include "AutoScenarioDef.h"
#include "CalaosConfig.h"
#include "EventManager.h"
#include "FileUtils.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Calaos;

namespace
{

/* NOT a third configuration file, and it must never become one - io.xml and
 * rules.xml are the two files everybody knows. This is a one shot breadcrumb
 * the server writes for itself, consumed and deleted at the next startup. Its absence
 * means "no upload to account for", which is the ordinary case, so losing it
 * costs an alert and nothing else. It lives under backups/ because that is
 * where the rest of what a `config put` preserves already goes.
 */
string uploadSnapshotPath()
{
    return Utils::getConfigFile("backups") + "/autoscenario_upload.json";
}

}

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
    //Front to back, as before. The room is moved out of the vector *first*
    //so that its destructor (which cascades into its IOs, and from there
    //into the event manager and the rule list) never runs on an element
    //that is still half-present in `rooms`.
    while (!rooms.empty())
    {
        std::unique_ptr<Room> room = std::move(rooms.front());
        rooms.erase(rooms.begin());
    }
}

bool ListeRoom::isHashRegistered(IOBase *io) const
{
    if (!io) return false;

    auto it = io_table.find(io->get_param("id"));
    return it != io_table.end() && it->second == io;
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
    //Ownership transfer in.
    if (!p)
    {
        cErrorDom("room") << "Add(): ignoring a null room";
        return;
    }

    rooms.emplace_back(p);

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

    //Same care as in the destructor: take the room out of the vector before
    //destroying it, so nothing it triggers on its way out (EventIODeleted
    //handlers, rule cleanup) can observe a half-erased rooms vector.
    std::unique_ptr<Room> room = std::move(rooms[pos]);
    rooms.erase(rooms.begin() + pos);

    cDebugDom("room");
}

Room *ListeRoom::operator[] (int i) const
{
    if (i < 0 || (uint)i >= rooms.size())
        return nullptr;

    return rooms[i].get();
}

Room *ListeRoom::get_room(int i)
{
    if (i < 0 || (uint)i >= rooms.size())
        return nullptr;

    return rooms[i].get();
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
            //Non-owning observation, the room keeps the ownership.
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

//See the contract on the declaration: del = false is an ownership TRANSFER.
bool ListeRoom::delete_io(IOBase *io, bool del)
{
    //A null needle must not be looked for: a room slot never holds null, but
    //answering "not found" up front keeps the intent explicit.
    if (!io) return false;

    bool done = false;
    for (uint j = 0;!done && j < rooms.size();j++)
    {
        for (int m = 0;!done && m < get_room(j)->get_size();m++)
        {
            IOBase *delio = get_room(j)->get_io(m);
            if (delio == io)
            {
                //Room::RemoveIO() is where the two ownership outcomes live:
                //destroy (del) or release to the caller (!del).
                get_room(j)->RemoveIO(m, del);
                done = true;
            }
        }
    }

    //false means "no room owns this IO", and nothing was destroyed: a second
    //call on an already removed IO is a harmless no-op, never a double free.
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
    //Before the generator touches anything: what an upload took away is
    //measured on the configuration exactly as it was loaded.
    reportAutoScenariosLostByUpload();

    list<Scenario *>::iterator it = auto_scenario_cache.begin();

    for (;it != auto_scenario_cache.end();it++)
    {
        Scenario *sc = *it;
        if (sc->getAutoScenario())
            sc->getAutoScenario()->checkScenarioRules();
    }

    /* DO NOT REINTRODUCE A SWEEP HERE. There used to be one, destroying every
     * rule carrying `auto_scenario` that no AutoScenario had adopted - and
     * SaveConfigRule() below persisted the destruction, at the first startup,
     * with no user action. It only made sense while rules were ADOPTED; they
     * are regenerated now, so an unclaimed rule is somebody else's data, not
     * an orphan.
     */

    //Must run before the two Save below: they are what persist the flag.
    refreshBrokenScenarios();

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();
}

vector<ListeRoom::KnownAutoScenario> ListeRoom::knownAutoScenarios()
{
    vector<KnownAutoScenario> known;

    for (Scenario *sc: auto_scenario_cache)
    {
        if (!sc || !sc->getAutoScenario()) continue;

        AutoScenarioDef *def = sc->getDefinition();
        if (!def || !def->isDefined()) continue;

        known.push_back({ def->uid, sc->get_param("name"), def->steps.size() });
    }

    return known;
}

void ListeRoom::snapshotAutoScenariosBeforeUpload()
{
    Json scenarios = Json::array();
    for (const KnownAutoScenario &k: knownAutoScenarios())
        scenarios.push_back(Json{{ "uid", k.uid },
                                 { "name", k.name },
                                 { "steps", k.stepCount }});

    //Written even when there is nothing to record: "the server had no
    //scenario" is what tells the next startup not to blame the upload for a
    //house that never had one.
    const string folder = Utils::getConfigFile("backups");
    if (!FileUtils::mkpath(folder))
    {
        cErrorDom("root") << "snapshotAutoScenariosBeforeUpload(): unable to create "
                          << folder << ", an upload losing a scenario will go unreported";
        return;
    }

    std::ofstream ofs(uploadSnapshotPath().c_str(), std::ios::out | std::ios::trunc);
    if (!ofs.is_open())
    {
        cErrorDom("root") << "snapshotAutoScenariosBeforeUpload(): unable to write "
                          << uploadSnapshotPath();
        return;
    }

    ofs << Json{{ "scenarios", scenarios }}.dump();
}

void ListeRoom::reportAutoScenariosLostByUpload()
{
    const string path = uploadSnapshotPath();

    std::ifstream ifs(path.c_str());
    if (!ifs.is_open())
        return; //ordinary startup, no upload behind it

    std::ostringstream content;
    content << ifs.rdbuf();
    ifs.close();

    //One shot, whatever the comparison says: a snapshot left behind would
    //re-announce at every boot a loss the user was already told about and
    //cannot undo any more.
    std::error_code ec;
    std::filesystem::remove(path, ec);

    const Json doc = Json::parse(content.str(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object() || !doc["scenarios"].is_array())
    {
        cErrorDom("root") << "reportAutoScenariosLostByUpload(): unreadable snapshot, "
                          << "the last upload cannot be accounted for";
        return;
    }

    vector<KnownAutoScenario> before;
    for (const Json &entry: doc["scenarios"])
    {
        if (!entry.is_object()) continue;
        before.push_back({ entry.value("uid", string()),
                           entry.value("name", string()),
                           entry.value("steps", (size_t)0) });
    }

    const vector<string> lost = diffLostAutoScenarios(before, knownAutoScenarios());
    if (lost.empty())
        return;

    string report = "The configuration that was uploaded no longer carries scenario "
                    "data this server had:\n";
    for (const string &line: lost)
        report += "\n" + line;

    /* Says what was NOT done as much as what was: the upload was applied as
     * sent, on purpose, and the way back is the backup rather than an undo.
     */
    report += "\n\nNothing was refused and nothing was undone: the configuration is "
              "the one that was uploaded. The one that was in place before it was "
              "backed up first, under " + Utils::getConfigFile("backups") + ".";

    cError() << lost.size() << " scenario(s) lost by the last configuration upload";
    Config::Instance().reportConfigAlert(report);
}

vector<string> ListeRoom::diffLostAutoScenarios(const vector<KnownAutoScenario> &before,
                                                const vector<KnownAutoScenario> &after)
{
    vector<string> lost;

    for (const KnownAutoScenario &was: before)
    {
        if (was.uid.empty()) continue;

        const KnownAutoScenario *now = nullptr;
        for (const KnownAutoScenario &k: after)
        {
            if (k.uid != was.uid) continue;
            now = &k;
            break;
        }

        /* The name the user gave it comes first and the uid is the fallback:
         * an alert naming something only the model recognizes sends the user
         * looking through io.xml for it.
         */
        string named = (now && !now->name.empty())? now->name: was.name;
        named = named.empty()? "'" + was.uid + "'"
                             : "'" + named + "' (" + was.uid + ")";

        if (!now)
            lost.push_back("- scenario " + named +
                           " is gone from the uploaded configuration");
        else if (now->stepCount < was.stepCount)
            lost.push_back("- scenario " + named + " lost " +
                           std::to_string(was.stepCount - now->stepCount) +
                           " of its " + std::to_string(was.stepCount) + " steps");
    }

    return lost;
}

Room * ListeRoom::searchRoomByNameAndType(string name, string type)
{
    Room *r = NULL;

    for (auto itRoom = rooms.begin(); itRoom != rooms.end() && !r; itRoom++)
        if( (*itRoom)->get_name() == name && (*itRoom)->get_type() == type)
            r = itRoom->get();

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
                r = rooms[j].get();
        }
    }

    return r;
}

//Rule-side cleanup only, no ownership change. Split out of deleteIO() so that
//Room's destructor can run exactly the same unlinking on the IOs it is about
//to destroy, without asking ListeRoom to locate a room that is dying.
void ListeRoom::detachIOFromRules(IOBase *io, bool modify, RuleDetachPolicy policy)
{
    if (!io) return;

    //first deal with all rules using "input": disabled by default (T3.18),
    //destroyed at the teardown sites that ask for it explicitly
    if (!modify) //only touches the rules if modify is not set
        ListeRule::Instance().RemoveRule(io, policy);

    /* Remove input from the polling list.
     *
     * E4.2c: unconditionally, i.e. driven by WHO REGISTERED and not by what
     * the IO looks like. This used to be gated on a hardcoded gui_type
     * whitelist ("time", "temp", "analog_in", "time_range", "timer") while
     * registration is done by the IO itself (ListeRule::Add(this) in
     * InputTime, InputAnalog, InPlageHoraire...). The two lists only agreed by
     * luck: any IO registering with a gui_type outside the whitelist - a new
     * driver, or an existing one whose gui_type is changed - stayed in
     * `in_event` after being destroyed, and ListeRule::RunEventLoop() then
     * dereferenced a freed pointer.
     * ListeRule::Remove(IOBase*) is an erase-remove: it is a no-op for an IO
     * that never registered, so "unregister always" is exactly "unregister
     * whoever registered". */
    ListeRule::Instance().Remove(io);
}

bool ListeRoom::deleteIO(IOBase *io, bool modify, RuleDetachPolicy policy)
{
    //Most callers pass a resolution result straight in (get_io()/findIO(),
    //often through a dynamic_cast). A miss must be a plain false, not a
    //dereference of nullptr in the gui_type test below.
    if (!io)
    {
        cErrorDom("root") << "deleteIO(): called with no IO, nothing to delete";
        return false;
    }

    detachIOFromRules(io, modify, policy);

    //Destroys the IO through its owning room. false when no room owns it,
    //and then nothing was destroyed (the caller still holds a live IO).
    const bool ret = delete_io(io);

    /* T3.18. AFTER the IO is really gone, and only when the rules were kept:
     * in Destroy mode there is nothing to detect (the rules that could tell are
     * the ones that have just been erased), and doing it before delete_io()
     * would flag the scenario that is itself being deleted - the delete path
     * runs AutoScenario::deleteAll() first, which leaves its own rule
     * references dangling until its IO leaves the cache.
     *
     * `ret` is tested too: delete_io() answers false when no room owns the IO,
     * and then NOTHING was destroyed - the caller still holds a live IO. The
     * rules referencing it have already been marked by detachIOFromRules() at
     * that point, so running the pass would disable a scenario, and PERSIST it,
     * for an IO that is still there. Practically unreachable (every caller
     * hands in an IO it just resolved out of a room), which is why it costs one
     * word here rather than a rollback of the marking.
     *
     * `modify` is deliberately not tested: with modify=true no rule was
     * touched, so no scenario can have become broken and the pass is a no-op.
     */
    if (ret && policy == RuleDetachPolicy::Disable)
        refreshBrokenScenarios();

    return ret;
}

void ListeRoom::forgetIOInAutoScenarios(IOBase *io)
{
    if (!io) return;

    for (Scenario *sc: auto_scenario_cache)
    {
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (as) as->forgetIO(io);
    }
}

void ListeRoom::forgetRoomInAutoScenarios(Room *room)
{
    if (!room) return;

    for (Scenario *sc: auto_scenario_cache)
    {
        AutoScenario *as = sc? sc->getAutoScenario(): nullptr;
        if (as) as->forgetRoom(room);
    }
}

void ListeRoom::refreshBrokenScenarios()
{
    //A copy: setDisabledMissingIo() raises events and stopBrokenRun() sets IOs,
    //which runs rules - none of them touches the cache today, but iterating the
    //member list while executing arbitrary rules is not something to rely on.
    list<Scenario *> scenarios = auto_scenario_cache;

    for (Scenario *sc: scenarios)
    {
        if (!sc) continue;

        AutoScenario *as = sc->getAutoScenario();
        if (!as || !as->isBroken()) continue;

        //ONLY EVER SETS. Already flagged: nothing to do, and above all nothing
        //to clear - the disabling is sticky until an explicit re-enable.
        if (as->isDisabledMissingIo()) continue;

        cWarningDom("scenario") << "Scenario '" << sc->get_param("name") << "' ("
                                << sc->get_param("id") << ") is DISABLED: it "
                                << "references IO(s) that do not exist ("
                                << as->getMissingIoDescription() << "). It will not "
                                << "run until it is re-enabled explicitly.";

        as->setDisabledMissingIo(true);

        //A scenario broken in the middle of a run would stay "in progress" for
        //ever otherwise
        as->stopBrokenRun();

        EventManager::create(CalaosEvent::EventScenarioChanged,
                             { { "id", sc->get_param("id") } });
    }
}

IOBase* ListeRoom::createIO(Params param, Room *room)
{
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

    //Sole owner until the room takes over, so no path out of this function
    //can leak the object (there is no `delete` left here).
    std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO(type, param));

    //E4.2b: the test is "did addIOHash() accept this IO", asked directly of
    //io_table, and no longer "is it resolvable by id". The E4.2a `!id.empty()`
    //clause is gone: it claimed to reproduce the historical behavior but did
    //the opposite. findIO() refuses to resolve an empty id by design, so with
    //an id-less IO the old test `findIO(id) != io` was always true, and the
    //clause was needed to stop the FIRST id-less IO from being destroyed
    //although addIOHash() had accepted it under the "" key. The price was
    //that a SECOND id-less IO — which addIOHash() does reject — was kept and
    //attached to the room while absent from io_table, i.e. exactly the
    //half-added state the comment below forbids (and, before E4.2a, it was
    //destroyed as a duplicate). Asking io_table directly answers both cases
    //correctly and needs no special case at all.
    if (io && !isHashRegistered(io.get()))
    {
        //addIOHash() rejected this IO because its id collided with an
        //already registered one. The object was still fully built by
        //IOFactory, so it must not be left half-added: never attach it to
        //room, and destroy it so it isn't reachable from anywhere while
        //being absent from io_table.
        cErrorDom("root") << "createIO(): discarding IO '" << id
                          << "', duplicate id was rejected by addIOHash()";
        io.reset();
    }

    if (io)
    {
        //Ownership transfer: from here on the room is the owner, and the
        //pointer we return is a plain non-owning observation.
        IOBase *added = io.release();
        room->AddIO(added);

        EventManager::create(CalaosEvent::EventIOAdded,
                             { { "id", id },
                               { "room_name", room->get_name() },
                               { "room_type", room->get_type() } });

        return added;
    }

    return nullptr;
}
