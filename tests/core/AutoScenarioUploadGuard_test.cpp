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
/*******************************************************************************
 * E4.6h - DEFENCE IN DEPTH ON THE UPLOAD PATH (docs/refactoring/E4.6.md, D10).
 *
 * ---------------------------------------------------------------------------
 * THE VECTOR THIS FILE IS ABOUT
 * ---------------------------------------------------------------------------
 * RC9: the hostile writer is the legitimate client. calaos_installer does not
 * call the autoscenario API at all - it downloads io.xml and rules.xml,
 * regenerates them in full from its own model, and uploads both through
 * `config put`. Whatever it did not model is gone, and the server receives a
 * perfectly coherent pair of files.
 *
 * D10 answers in levels. Level 0 (the definition is regenerated at every load)
 * landed with E4.6b/c. This file is levels 1 and 2:
 *
 *   LEVEL 1 - the configuration is BACKED UP before it is overwritten, and the
 *             backup is USABLE. "A backup exists" is not the property; "the
 *             lost scenario comes back out of it" is.       -> section 1
 *   LEVEL 2 - an upload that drops a known scenario, or takes steps away from
 *             one, raises a configuration alert NAMING it.  -> section 2
 *
 * ⛔ LEVEL 3 - refusing the upload - IS RULED OUT BY DECISION. Deleting a
 * scenario from the installer has to keep working, and a server that refuses
 * on the state of the world before the write locks the user in (the exact
 * reasoning that ruled out refusing `modify`, E4.6.md §10 Q1). The server
 * signals, it does not arbitrate. Section 4 is the standing proof of that: a
 * legitimate deletion goes through untouched.
 *
 * ---------------------------------------------------------------------------
 * WHY THE ALERT CANNOT BE RAISED AT UPLOAD TIME
 * ---------------------------------------------------------------------------
 * A successful `config put` sets need_restart, HttpClient::DataWritten() stops
 * the event loop as soon as the reply is out, and a short timer stops it anyway
 * if the client has gone (JsonApi::exitAfterConfigReplaced()).
 * The alert channel is deferred by 30 seconds on purpose (CalaosConfig.cpp,
 * CONFIG_ALERT_DELAY_SEC) so that it fires with the loop up and the notifier
 * usable. An alert queued during the put would therefore be thrown away by the
 * restart, every time. The finding has to survive to the NEXT boot, which is
 * what the tests below play: put, then a full clear/load/checkAutoScenario().
 *
 * ---------------------------------------------------------------------------
 * THE FIXTURE IS RICH ON PURPOSE - DO NOT MAKE IT SMALLER
 * ---------------------------------------------------------------------------
 * "Fixture pauvre" is the most frequent defect of this series (E4.6.md §9.3),
 * and an alert is where it bites hardest. So:
 *
 *   - TWO scenarios, with DIFFERENT names and DIFFERENT step counts (3 and 1),
 *     so a diff that reports the wrong one, or that reports both, fails;
 *   - the one that disappears is NOT the one declared first;
 *   - three different pauses and six different action values, so a restore
 *     that brings back a scenario-shaped object rather than THE scenario fails;
 *   - and, section 3, the fixture that is always missing on an alert: THE ONE
 *     WHERE THERE IS NOTHING TO ALERT ABOUT. An upload that loses nothing, an
 *     upload from a house that never had a scenario, and an ordinary boot with
 *     no upload behind it must all be silent. Without those three, a report
 *     that fires unconditionally reads exactly like a report that fires on a
 *     loss.
 *
 * ---------------------------------------------------------------------------
 * PROBES AND IDS
 * ---------------------------------------------------------------------------
 * Ids are e46h_ prefixed: Config's IO state cache is process wide and never
 * cleared, so sharing an id with another test binary leaks state. The scenario
 * IO ids and the uids are NOT hardcoded, they are read back from the objects
 * production minted - what is characterized here is the diff, not the id
 * allocator.
 *
 * ---------------------------------------------------------------------------
 * EVENT QUEUE
 * ---------------------------------------------------------------------------
 * No pump in this file, and that is measured, not assumed: no case here
 * asserts anything about a delivered message. The harness leaves the queue
 * empty between cases (E4.0g). If you ever need a pump, measure what it
 * absorbs or do not add it.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "AdminConfig.h"
#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "CalaosConfig.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Scenario.h"
#include "Timer.h"
#include "libuvw.h"

#include <pugixml.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char ROOM_NAME_E46H[] = "E4.6h room";
const char ROOM_TYPE_E46H[] = "salon";

const char IO_LAMP[] = "e46h_lamp";
const char IO_BLIND[] = "e46h_blind";
const char IO_VOLUME[] = "e46h_volume";
const char IO_BANNER[] = "e46h_banner";
const char IO_SIREN[] = "e46h_siren";

//The two scenarios of the fixture. Different names, different step counts, and
//the one made to disappear is the one declared FIRST - so a diff that answers
//"the last one" or "the only one" cannot pass.
const char NAME_EVENING[] = "Soir\xc3\xa9""e";   //3 steps, accented on purpose
const char NAME_WAKEUP[] = "R\xc3\xa9veil";      //1 step

std::string houseIosXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", IO_LAMP, "Lampe");
    ios += internalIoXml("InternalInt", IO_BLIND, "Volet");
    ios += internalIoXml("InternalInt", IO_VOLUME, "Volume");
    ios += internalIoXml("InternalString", IO_BANNER, "Banniere");
    ios += internalIoXml("InternalBool", IO_SIREN, "Sirene");
    return ios;
}

} //namespace

class AutoScenarioUploadGuardTest: public JsonApiCharacterizationTest
{
protected:
    void loadHouse()
    {
        loadConfig(ioXmlDocument(roomXml(ROOM_NAME_E46H, ROOM_TYPE_E46H, houseIosXml(), 0)),
                   rulesXmlDocument(std::string()));
    }

    static Scenario *scenarioIo(const std::string &id)
    {
        return dynamic_cast<Scenario *>(ListeRoom::Instance().get_io(id));
    }

    static std::string uidOf(const std::string &ioId)
    {
        Scenario *sc = scenarioIo(ioId);
        AutoScenarioDef *def = sc? sc->getDefinition(): nullptr;
        return def? def->uid: std::string();
    }

    static size_t stepCountOf(const std::string &ioId)
    {
        Scenario *sc = scenarioIo(ioId);
        AutoScenarioDef *def = sc? sc->getDefinition(): nullptr;
        return def? def->steps.size(): 0u;
    }

    Json wsAutoscenario(WsTestSession &ws, Json data)
    {
        ws.clear();
        ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "e46h" }, { "data", data }});
        EXPECT_EQ(1u, ws.count()) << "autoscenario " << data.value("type", std::string())
                                  << " answered " << ws.count() << " messages";
        if (ws.count() != 1) return Json::object();
        return ws.lastData();
    }

    /* THE THREE STEP SCENARIO. Every asymmetry of it is load bearing: three
     * different pauses, six values that look nothing alike, one IO targeted
     * twice with two different values.
     */
    std::string createEveningScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", NAME_EVENING },
            { "room_name", ROOM_NAME_E46H },
            { "room_type", ROOM_TYPE_E46H },
            { "steps", Json::array({
                Json{{ "pause", "1.5" },
                     { "actions", Json::array({
                         Json{{ "io", IO_LAMP }, { "value", "true" }} }) }},
                Json{{ "pause", "2.25" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BLIND }, { "value", "77" }},
                         Json{{ "io", IO_VOLUME }, { "value", "42" }} }) }},
                Json{{ "pause", "0.5" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BANNER }, { "value", "bonsoir" }} }) }}
            }) },
            { "final_step", Json{{ "actions", Json::array({
                         Json{{ "io", IO_SIREN }, { "value", "false" }},
                         Json{{ "io", IO_LAMP }, { "value", "false" }} }) }} }});

        return ret.value("id", std::string());
    }

    //The one step one. Deliberately nothing like the other.
    std::string createWakeupScenario(WsTestSession &ws)
    {
        const Json ret = wsAutoscenario(ws, Json{
            { "type", "create" },
            { "name", NAME_WAKEUP },
            { "room_name", ROOM_NAME_E46H },
            { "room_type", ROOM_TYPE_E46H },
            { "steps", Json::array({
                Json{{ "pause", "3.75" },
                     { "actions", Json::array({
                         Json{{ "io", IO_BLIND }, { "value", "12" }} }) }}
            }) },
            { "final_step", Json{{ "actions", Json::array({
                         Json{{ "io", IO_BANNER }, { "value", "bonjour" }} }) }} }});

        return ret.value("id", std::string());
    }

    /* ---------------------------------------------------------------------
     * XML surgery, pugixml rather than string replacement: the attribute
     * order of a saved document is an implementation detail of pugixml.
     * ------------------------------------------------------------------ */

    static std::string serialize(const pugi::xml_document &doc)
    {
        std::ostringstream ss;
        doc.save(ss);
        return ss.str();
    }

    static bool parse(const std::string &xml, pugi::xml_document &doc)
    {
        return doc.load_buffer(xml.data(), xml.size());
    }

    static std::vector<pugi::xml_node> ioNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node room: doc.child("calaos:ioconfig").child("calaos:home"))
            for (pugi::xml_node io: room)
                out.push_back(io);
        return out;
    }

    static std::vector<pugi::xml_node> ruleNodes(pugi::xml_document &doc)
    {
        std::vector<pugi::xml_node> out;
        for (pugi::xml_node r: doc.child("calaos:rules"))
            out.push_back(r);
        return out;
    }

    /* The marker of a scenario IO. Since T3.61 it is the definition uid, and
     * the machinery IOs and the generated rules carry that same value - the
     * machinery under the legacy key, which is the one place the server still
     * writes it (E4.6.md §5.3). It is therefore still the handle on
     * "everything that belongs to this scenario" in both files at once.
     */
    static std::string markerOf(const std::string &xml, const std::string &ioId)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return {};

        for (pugi::xml_node io: ioNodes(doc))
            if (std::string(io.attribute("id").value()) == ioId)
                return io.attribute(AutoScenarioDef::KEY_UID).value();
        return {};
    }

    /* Delete a whole scenario from an io.xml: its Scenario IO, its definition
     * (params of that IO) and its internal IOs. This is what an installer that
     * does not model the scenario any more uploads.
     * Answers how many IO elements were removed.
     */
    static int removeScenarioFromIoXml(std::string &xml, const std::string &marker)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc) || marker.empty()) return 0;

        int removed = 0;
        for (pugi::xml_node io: ioNodes(doc))
        {
            //Either key: the scenario IO carries the uid, its machinery the
            //legacy one, and both hold the same value.
            if (std::string(io.attribute("auto_scenario").value()) != marker &&
                std::string(io.attribute(AutoScenarioDef::KEY_UID).value()) != marker)
                continue;
            io.parent().remove_child(io);
            removed++;
        }

        if (removed) xml = serialize(doc);
        return removed;
    }

    //The rules.xml half of the same removal.
    static int removeScenarioFromRulesXml(std::string &xml, const std::string &marker)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc) || marker.empty()) return 0;

        std::vector<pugi::xml_node> victims;
        for (pugi::xml_node r: ruleNodes(doc))
            if (std::string(r.attribute("auto_scenario").value()) == marker)
                victims.push_back(r);

        for (pugi::xml_node v: victims)
            v.parent().remove_child(v);

        if (!victims.empty()) xml = serialize(doc);
        return (int)victims.size();
    }

    /* Take the LAST step away from a definition carried by io.xml, the way a
     * tool that models fewer steps than the server does would.
     * `autoscenario_steps` is authoritative (E4.6b), so dropping its last
     * token is the whole amputation; the `as_<id>_*` params it stops naming
     * are dropped here too, as an installer regenerating the file would.
     */
    static bool dropLastStepFromIoXml(std::string &xml, const std::string &ioId)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        for (pugi::xml_node io: ioNodes(doc))
        {
            if (std::string(io.attribute("id").value()) != ioId) continue;

            std::string steps = io.attribute("autoscenario_steps").value();
            const size_t bar = steps.rfind('|');
            if (bar == std::string::npos) return false;

            const std::string dropped = steps.substr(bar + 1);
            steps.erase(bar);
            io.attribute("autoscenario_steps").set_value(steps.c_str());
            io.remove_attribute(("as_" + dropped + "_pause").c_str());
            io.remove_attribute(("as_" + dropped + "_actions").c_str());

            xml = serialize(doc);
            return true;
        }
        return false;
    }

    //One more IO in the room, so an upload can differ from what is on disk
    //without losing anything.
    static bool addIoToIoXml(std::string &xml, const std::string &id)
    {
        pugi::xml_document doc;
        if (!parse(xml, doc)) return false;

        pugi::xml_node home = doc.child("calaos:ioconfig").child("calaos:home");
        pugi::xml_node room = home.first_child();
        if (!room) return false;

        pugi::xml_node io = room.append_child("calaos:internal");
        io.append_attribute("id").set_value(id.c_str());
        io.append_attribute("name").set_value("Ajout");
        io.append_attribute("type").set_value("InternalBool");
        io.append_attribute("gui_type").set_value("bool");
        io.append_attribute("io_type").set_value("inout");
        io.append_attribute("visible").set_value("true");

        xml = serialize(doc);
        return true;
    }

    /* ---------------------------------------------------------------------
     * The two moves of the vector: the upload, and the reboot after it
     * ------------------------------------------------------------------ */

    //`config put` over HTTP, the real handler. Answers the parsed reply.
    Json uploadConfig(const std::string &ioXml, const std::string &rulesXml)
    {
        HttpTestRequest req;
        req.send(authenticated(Json{
            { "action", "config" }, { "type", "put" },
            { "config_files", Json{{ "io.xml", ioXml }, { "rules.xml", rulesXml }} }}));

        EXPECT_EQ(1u, req.count());
        EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
        if (req.count() != 1) return Json::object();
        return req.bodyJson();
    }

    //What the server does next: it restarts, and starts again on the files the
    //upload left on disk.
    void rebootOnDiskConfig()
    {
        const std::string ioXml = ioXmlOnDisk();
        const std::string rulesXml = rulesXmlOnDisk();

        clearCoreState();
        loadConfig(ioXml, rulesXml);
        ListeRoom::Instance().checkAutoScenario();
    }

    //Timers left by the puts of earlier tests would stop the loop in their place.
    void letEarlierRestartTimersExpire()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        uvw::Loop::getDefault()->run<uvw::Loop::Mode::NOWAIT>();
    }

    //Whether something stops the loop within `ms`. HttpTestRequest never writes
    //to a socket, so its client plays one that left before the reply.
    bool loopStopsWithin(int ms)
    {
        auto loop = uvw::Loop::getDefault();
        bool expired = false;
        Timer guard(ms / 1000.0, [&expired, &loop]()
                    {
            expired = true;
            loop->stop();
        });
        loop->run();
        return !expired;
    }

    /* ---------------------------------------------------------------------
     * Observables
     * ------------------------------------------------------------------ */

    //Every copy of `name` under <config>/backups, newest first by mtime. Same
    //walk as findBackupsNewestFirst() (CalaosConfig.cpp:55-82), reimplemented
    //here because that one is file static.
    std::vector<std::string> backupsOf(const std::string &name) const
    {
        namespace fs = std::filesystem;
        std::vector<std::pair<fs::file_time_type, std::string>> found;

        std::error_code ec;
        fs::recursive_directory_iterator it(configDir() + "/backups",
                                            fs::directory_options::skip_permission_denied,
                                            ec), end;
        for (;!ec && it != end;it.increment(ec))
        {
            std::error_code fec;
            if (!it->is_regular_file(fec) || fec) continue;
            if (it->path().filename() != name) continue;
            auto t = fs::last_write_time(it->path(), fec);
            if (fec) continue;
            found.emplace_back(t, it->path().string());
        }

        std::sort(found.begin(), found.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });

        std::vector<std::string> out;
        for (auto &f: found) out.push_back(std::move(f.second));
        return out;
    }

    //The folder name Config::BackupFiles() derives from the clock. Two uploads
    //that read the same stamp are the collision these cases are about.
    static std::string backupSecondStamp()
    {
        const std::time_t t = std::time(nullptr);
        const std::tm tm = *std::localtime(&t);
        std::ostringstream ss;
        ss << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");
        return ss.str();
    }

    //Return at the start of a second, so what follows has a full second of
    //margin to run inside one stamp instead of hoping for it.
    static void alignToNextSecond()
    {
        const std::string start = backupSecondStamp();
        while (backupSecondStamp() == start)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    static std::string readWholeFile(const std::string &path)
    {
        std::ifstream f(path.c_str());
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    //Config is a process wide singleton and its alert queue is only emptied
    //when the notification is really sent: every case reads the DELTA it
    //caused, never the whole queue.
    static size_t alertCount()
    {
        return Config::Instance().getConfigAlerts().size();
    }

    static std::string lastAlert()
    {
        const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
        return alerts.empty()? std::string(): alerts.back();
    }
};

/*******************************************************************************
 * SECTION 1 - D10 LEVEL 1: THE BACKUP, AND WHETHER IT IS WORTH ANYTHING
 *
 * `JsonApiHandlerHttp::processConfig()` calls Config::BackupFiles() before it
 * writes the first received byte. E4.6a pinned that the call happens and that
 * the backup carries the pre-put content. These two cases go one step further,
 * because "a file exists under backups/" is not the property an operator needs:
 * the property is that the configuration comes BACK out of it.
 ******************************************************************************/

TEST_F(AutoScenarioUploadGuardTest, ADestructiveUploadLeavesABackupThatBringsTheLostScenarioBackWhole)
{
    /* ✅ PROVE, DO NOT FLIP - D10 level 1.
     *
     * The proof is by USE: the two backed up files are loaded back through the
     * real Config path and the scenario is read back through the API. Whole
     * means whole - three steps, the three pauses, the two actions of the
     * middle step, in order.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    ASSERT_FALSE(eveningId.empty());
    ASSERT_FALSE(wakeupId.empty());
    saveConfig();

    const std::string ioBefore = ioXmlOnDisk();
    const std::string rulesBefore = rulesXmlOnDisk();
    ASSERT_TRUE(backupsOf("io.xml").empty()) << "the case starts with no backup";

    //the shape of what an installer that lost everything sends back
    const Json reply = uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                    rulesXmlDocument(std::string()));
    ASSERT_EQ("true", str(reply, "success"));

    const std::vector<std::string> ioBackups = backupsOf("io.xml");
    const std::vector<std::string> ruleBackups = backupsOf("rules.xml");
    ASSERT_EQ(1u, ioBackups.size()) << "no backup under " << configDir() << "/backups";
    ASSERT_EQ(1u, ruleBackups.size());
    ASSERT_EQ(ioBefore, readWholeFile(ioBackups[0]));
    ASSERT_EQ(rulesBefore, readWholeFile(ruleBackups[0]));

    /* The overwrite really happened - measured on the FILE. The live objects
     * are deliberately untouched by a put: the server restarts instead of
     * reloading in place (HttpClient.cpp:476-481), so nothing in memory is
     * expected to have moved at this point.
     */
    ASSERT_EQ(std::string::npos, ioXmlOnDisk().find(AutoScenarioDef::KEY_UID));
    ASSERT_NE(std::string::npos, ioBefore.find(AutoScenarioDef::KEY_UID));

    //restore, the way an operator does
    clearCoreState();
    loadConfig(readWholeFile(ioBackups[0]), readWholeFile(ruleBackups[0]));
    ListeRoom::Instance().checkAutoScenario();

    Scenario *sc = scenarioIo(eveningId);
    ASSERT_TRUE(sc != nullptr) << "the restored configuration has no scenario";
    ASSERT_TRUE(sc->getAutoScenario() != nullptr);
    EXPECT_EQ(NAME_EVENING, sc->get_param("name"));

    WsTestSession ws;
    const Json got = wsAutoscenario(ws, Json{{ "type", "get" }, { "id", eveningId }});
    ASSERT_EQ(3u, got["steps"].size()) << got.dump();
    EXPECT_EQ("1.5", got["steps"][0].value("pause", std::string()));
    EXPECT_EQ("2.25", got["steps"][1].value("pause", std::string()));
    EXPECT_EQ("0.5", got["steps"][2].value("pause", std::string()));
    ASSERT_EQ(2u, got["steps"][1]["actions"].size());
    EXPECT_EQ(IO_BLIND, got["steps"][1]["actions"][0].value("io", std::string()));
    EXPECT_EQ("77", got["steps"][1]["actions"][0].value("value", std::string()));
    EXPECT_EQ(IO_VOLUME, got["steps"][1]["actions"][1].value("io", std::string()));
    EXPECT_EQ("42", got["steps"][1]["actions"][1].value("value", std::string()));
    EXPECT_EQ("false", got.value("broken", std::string()));

    //and the other scenario came back too, at its own size
    EXPECT_EQ(1u, stepCountOf(wakeupId));
}

TEST_F(AutoScenarioUploadGuardTest, TwoUploadsLeaveTwoBackupsAndTheNewestIsTheStateJustBeforeTheLastOne)
{
    /* ✅ PROVE, DO NOT FLIP - the other half of "usable". An operator who
     * uploaded twice before noticing has to be able to pick the right copy,
     * which is what findBackupsNewestFirst() (CalaosConfig.cpp:55-82) orders
     * for. Both ends are measured: the NEWEST restores the one scenario that
     * survived the first upload, the OLDEST restores both.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    const std::string ioOriginal = ioXmlOnDisk();
    const std::string rulesOriginal = rulesXmlOnDisk();

    //first upload: the evening scenario goes, the wake up one stays
    const std::string marker = markerOf(ioOriginal, eveningId);
    ASSERT_FALSE(marker.empty());
    std::string ioOnce = ioOriginal, rulesOnce = rulesOriginal;
    ASSERT_GT(removeScenarioFromIoXml(ioOnce, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesOnce, marker), 0);
    ASSERT_EQ("true", str(uploadConfig(ioOnce, rulesOnce), "success"));

    //second upload, the restart between them having lifted the freeze: everything goes
    Utils::set_config_frozen(false);
    ASSERT_EQ("true", str(uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                       rulesXmlDocument(std::string())), "success"));

    const std::vector<std::string> ioBackups = backupsOf("io.xml");
    ASSERT_EQ(2u, ioBackups.size());
    const std::vector<std::string> ruleBackups = backupsOf("rules.xml");
    ASSERT_EQ(2u, ruleBackups.size());

    //newest first: the state between the two uploads
    clearCoreState();
    loadConfig(readWholeFile(ioBackups[0]), readWholeFile(ruleBackups[0]));
    ListeRoom::Instance().checkAutoScenario();
    EXPECT_TRUE(scenarioIo(eveningId) == nullptr) << "the newest backup is the older state";
    ASSERT_TRUE(scenarioIo(wakeupId) != nullptr);
    EXPECT_EQ(NAME_WAKEUP, scenarioIo(wakeupId)->get_param("name"));

    //and the oldest is the state before anything was uploaded
    clearCoreState();
    loadConfig(readWholeFile(ioBackups[1]), readWholeFile(ruleBackups[1]));
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_TRUE(scenarioIo(eveningId) != nullptr);
    EXPECT_EQ(3u, stepCountOf(eveningId));
    ASSERT_TRUE(scenarioIo(wakeupId) != nullptr);
    EXPECT_EQ(1u, stepCountOf(wakeupId));
}

TEST_F(AutoScenarioUploadGuardTest, TwoUploadsInsideTheSameSecondStillLeaveTheStateBeforeTheFirstOne)
{
    /* ✅ PROVE, DO NOT FLIP - the collision itself. Scripted uploads land
     * inside the same second, and the backup taken before the FIRST one is the
     * only copy of a configuration that then exists nowhere else. What is
     * asserted is its CONTENT, read back through the real Config path: counting
     * folders would pass on an empty or unreadable one.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    ASSERT_FALSE(eveningId.empty());
    ASSERT_FALSE(wakeupId.empty());
    saveConfig();

    const std::string ioOriginal = ioXmlOnDisk();
    const std::string rulesOriginal = rulesXmlOnDisk();
    ASSERT_TRUE(backupsOf("io.xml").empty()) << "the case starts with no backup";

    const std::string marker = markerOf(ioOriginal, eveningId);
    ASSERT_FALSE(marker.empty());
    std::string ioOnce = ioOriginal, rulesOnce = rulesOriginal;
    ASSERT_GT(removeScenarioFromIoXml(ioOnce, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesOnce, marker), 0);

    //A loaded host can push the pair over a second boundary, and then the case
    //proves nothing: start again from the original state, on a fresh second.
    std::string stamp;
    for (int attempt = 0; attempt < 10; attempt++)
    {
        if (attempt > 0)
        {
            std::error_code ec;
            std::filesystem::remove_all(configDir() + "/backups", ec);
            clearCoreState();
            loadConfig(ioOriginal, rulesOriginal);
            ListeRoom::Instance().checkAutoScenario();
            Utils::set_config_frozen(false);
        }

        alignToNextSecond();
        stamp = backupSecondStamp();

        //first upload: the evening scenario goes. Second: everything goes. No wait
        //between them - that is the whole point of the case. The restart that lifts
        //the freeze is not played: only the name of the backup folder is at stake.
        ASSERT_EQ("true", str(uploadConfig(ioOnce, rulesOnce), "success"));
        Utils::set_config_frozen(false);
        ASSERT_EQ("true", str(uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                           rulesXmlDocument(std::string())),
                              "success"));

        if (stamp == backupSecondStamp())
            break;
    }

    //without this the case would be vacuous: uploads that straddle a second
    //boundary do not collide and pass whatever BackupFiles() names its folder
    ASSERT_EQ(stamp, backupSecondStamp()) << "the two uploads did not land in the same second";

    const std::vector<std::string> ioBackups = backupsOf("io.xml");
    const std::vector<std::string> ruleBackups = backupsOf("rules.xml");
    ASSERT_EQ(2u, ioBackups.size()) << "the second upload overwrote the backup of the first";
    ASSERT_EQ(2u, ruleBackups.size());

    //the oldest holds the state before ANY upload, byte for byte
    EXPECT_EQ(ioOriginal, readWholeFile(ioBackups[1]));
    EXPECT_EQ(rulesOriginal, readWholeFile(ruleBackups[1]));

    //and it is usable: the real Config path reads both scenarios back out of it
    clearCoreState();
    loadConfig(readWholeFile(ioBackups[1]), readWholeFile(ruleBackups[1]));
    ListeRoom::Instance().checkAutoScenario();
    ASSERT_TRUE(scenarioIo(eveningId) != nullptr) << "the oldest backup lost the scenario";
    EXPECT_EQ(NAME_EVENING, scenarioIo(eveningId)->get_param("name"));
    EXPECT_EQ(3u, stepCountOf(eveningId));
    ASSERT_TRUE(scenarioIo(wakeupId) != nullptr);
    EXPECT_EQ(NAME_WAKEUP, scenarioIo(wakeupId)->get_param("name"));
    EXPECT_EQ(1u, stepCountOf(wakeupId));
}

/*******************************************************************************
 * SECTION 2 - D10 LEVEL 2: DETECT AND ALERT
 *
 * The server knows the uid, the name and the step count of every scenario it
 * had loaded. After an upload and the restart that follows it, it can say what
 * is missing - and it must say it by the name the user gave the scenario, not
 * by the uid the model files it under.
 ******************************************************************************/

TEST_F(AutoScenarioUploadGuardTest, AnUploadThatDropsAKnownScenarioAlertsAtTheNextStartupNamingIt)
{
    /* ✅ FLIPPED by E4.6h. The upload takes a whole scenario away; before this
     * ticket the server said nothing at all and the user found out the day the
     * shutters did not open. It now raises exactly ONE alert, naming the
     * scenario the way the user named it - and not naming the survivor.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    const std::string eveningUid = uidOf(eveningId);
    const std::string wakeupUid = uidOf(wakeupId);
    ASSERT_FALSE(eveningUid.empty());
    ASSERT_NE(eveningUid, wakeupUid);

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    const std::string marker = markerOf(ioXml, eveningId);
    ASSERT_GT(removeScenarioFromIoXml(ioXml, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesXml, marker), 0);

    ASSERT_EQ("true", str(uploadConfig(ioXml, rulesXml), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    ASSERT_TRUE(scenarioIo(eveningId) == nullptr);
    ASSERT_TRUE(scenarioIo(wakeupId) != nullptr) << "the wrong scenario was removed";

    ASSERT_EQ(alertsBefore + 1u, alertCount()) << "the loss raised no alert";
    const std::string report = lastAlert();

    EXPECT_NE(std::string::npos,
              report.find("- scenario '" + std::string(NAME_EVENING) + "' (" +
                          eveningUid + ") is gone from the uploaded configuration"))
            << report;

    //the survivor is named neither by its name nor by its uid
    EXPECT_EQ(std::string::npos, report.find(NAME_WAKEUP)) << report;
    EXPECT_EQ(std::string::npos, report.find(wakeupUid)) << report;

    //and the alert says what was NOT done: the upload stands
    EXPECT_NE(std::string::npos, report.find("Nothing was refused")) << report;
}

TEST_F(AutoScenarioUploadGuardTest, AnUploadThatTakesAStepAwayFromAKnownScenarioAlertsAndSaysHowMany)
{
    /* ✅ FLIPPED by E4.6h. The other half of the loss: the scenario is still
     * there, shorter. It is the worse of the two to spot - the scenario still
     * runs, it just stops doing one of the things it did - so the alert has to
     * say how many steps went, not merely that something changed.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    ASSERT_EQ(3u, stepCountOf(eveningId));
    const std::string eveningUid = uidOf(eveningId);
    ASSERT_FALSE(eveningUid.empty());

    std::string ioXml = ioXmlOnDisk();
    ASSERT_TRUE(dropLastStepFromIoXml(ioXml, eveningId));

    ASSERT_EQ("true", str(uploadConfig(ioXml, rulesXmlOnDisk()), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    ASSERT_TRUE(scenarioIo(eveningId) != nullptr);
    ASSERT_EQ(2u, stepCountOf(eveningId)) << "the step was not really taken away";
    ASSERT_EQ(1u, stepCountOf(wakeupId));

    ASSERT_EQ(alertsBefore + 1u, alertCount()) << "the amputation raised no alert";
    const std::string report = lastAlert();

    EXPECT_NE(std::string::npos,
              report.find("- scenario '" + std::string(NAME_EVENING) + "' (" +
                          eveningUid + ") lost 1 of its 3 steps")) << report;
    EXPECT_EQ(std::string::npos, report.find("is gone from")) << report;
    EXPECT_EQ(std::string::npos, report.find(NAME_WAKEUP)) << report;
}

TEST_F(AutoScenarioUploadGuardTest, AScenarioTheUserNeverNamedIsReportedByItsUid)
{
    /* ✅ FLIPPED by E4.6h. The fallback of the naming rule. A name is what the
     * user recognizes, so it comes first; but a scenario whose name is empty
     * must still be named by something, and the uid is all there is left.
     */
    loadHouse();

    std::string eveningId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
    }
    saveConfig();

    const std::string eveningUid = uidOf(eveningId);
    ASSERT_FALSE(eveningUid.empty());

    //blank the name on the live object, i.e. on what the server knows at the
    //moment the upload lands - not on the file, which is about to be replaced
    ASSERT_TRUE(scenarioIo(eveningId) != nullptr);
    scenarioIo(eveningId)->get_params().Add("name", "");

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    const std::string marker = markerOf(ioXml, eveningId);
    ASSERT_GT(removeScenarioFromIoXml(ioXml, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesXml, marker), 0);

    ASSERT_EQ("true", str(uploadConfig(ioXml, rulesXml), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    ASSERT_EQ(alertsBefore + 1u, alertCount());
    const std::string report = lastAlert();

    EXPECT_NE(std::string::npos,
              report.find("- scenario '" + eveningUid +
                          "' is gone from the uploaded configuration")) << report;
    //the uid REPLACED the name, it was not printed next to an empty one
    EXPECT_EQ(std::string::npos, report.find("scenario '' ")) << report;
}

TEST_F(AutoScenarioUploadGuardTest, AnUploadThatLosesTwoScenariosNamesBothOfThem)
{
    /* ✅ FLIPPED by E4.6h. Plural. A report that stops at the first loss is a
     * report that hides the second one, and the two scenarios of the fixture
     * have nothing in common on purpose - different names, different step
     * counts. One alert carries both lines, in the order they were declared.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    const std::string eveningUid = uidOf(eveningId);
    const std::string wakeupUid = uidOf(wakeupId);
    ASSERT_FALSE(eveningUid.empty());
    ASSERT_FALSE(wakeupUid.empty());

    const size_t alertsBefore = alertCount();
    ASSERT_EQ("true", str(uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                       rulesXmlDocument(std::string())), "success"));
    rebootOnDiskConfig();

    ASSERT_TRUE(scenarioIo(eveningId) == nullptr);
    ASSERT_TRUE(scenarioIo(wakeupId) == nullptr);

    ASSERT_EQ(alertsBefore + 1u, alertCount()) << "two losses must be one alert";
    const std::string report = lastAlert();

    const size_t evening =
            report.find("- scenario '" + std::string(NAME_EVENING) + "' (" +
                        eveningUid + ") is gone from the uploaded configuration");
    const size_t wakeup =
            report.find("- scenario '" + std::string(NAME_WAKEUP) + "' (" +
                        wakeupUid + ") is gone from the uploaded configuration");
    EXPECT_NE(std::string::npos, evening) << report;
    EXPECT_NE(std::string::npos, wakeup) << report;
    EXPECT_LT(evening, wakeup) << "the two lines are in declaration order";
}

TEST_F(AutoScenarioUploadGuardTest, TheAlertIsRaisedOnceAndTheStartupAfterItIsSilent)
{
    /* ✅ FLIPPED by E4.6h. A loss is news exactly once. Re-alerting at every
     * boot for something the user was already told about, and cannot undo any
     * more, turns the channel into noise - and this channel also carries the
     * corrupt-file recovery, which nobody may learn to ignore.
     */
    loadHouse();

    std::string eveningId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        createWakeupScenario(ws);
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    const std::string marker = markerOf(ioXml, eveningId);
    ASSERT_GT(removeScenarioFromIoXml(ioXml, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesXml, marker), 0);
    ASSERT_EQ("true", str(uploadConfig(ioXml, rulesXml), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();
    const size_t afterFirstBoot = alertCount();

    //a second boot on the very same files, no upload in between
    rebootOnDiskConfig();

    EXPECT_EQ(alertsBefore + 1u, afterFirstBoot) << "the first boot said nothing";
    EXPECT_EQ(afterFirstBoot, alertCount()) << "the second boot said it again";
}

/*******************************************************************************
 * SECTION 3 - NOTHING TO ALERT ABOUT
 *
 * The fixture that is always missing on an alert, and it is missing twice over
 * here: nothing is lost, and there was nothing to lose in the first place.
 * Without these, a report emitted unconditionally is indistinguishable from a
 * report emitted on a loss, and the mutation that removes the condition stays
 * green.
 ******************************************************************************/

TEST_F(AutoScenarioUploadGuardTest, AnUploadThatLosesNothingSaysNothing)
{
    /* ✅ PROVE, DO NOT FLIP. The upload is real - it adds an IO, so the files
     * on disk do change - and both scenarios come through it whole.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    ASSERT_TRUE(addIoToIoXml(ioXml, "e46h_added"));
    ASSERT_NE(ioXml, ioXmlOnDisk()) << "the upload has to be a real change";

    ASSERT_EQ("true", str(uploadConfig(ioXml, rulesXmlOnDisk()), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    ASSERT_TRUE(ListeRoom::Instance().get_io("e46h_added") != nullptr);
    ASSERT_EQ(3u, stepCountOf(eveningId));
    ASSERT_EQ(1u, stepCountOf(wakeupId));

    EXPECT_EQ(alertsBefore, alertCount())
            << "an upload that lost nothing reported something: " << lastAlert();
}

TEST_F(AutoScenarioUploadGuardTest, AnUploadFromAConfigurationThatHadNoScenarioSaysNothing)
{
    /* ✅ PROVE, DO NOT FLIP - and this is the case the "fixture pauvre" habit
     * leaves out: there is nothing to compare, because the server had nothing.
     * A report that is written before the comparison, or a header emitted
     * around an empty list, only shows up here.
     */
    loadHouse();
    saveConfig();
    ASSERT_TRUE(ListeRoom::Instance().getAutoScenarios().empty());

    ASSERT_EQ("true", str(uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                       rulesXmlDocument(std::string())), "success"));

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    EXPECT_EQ(alertsBefore, alertCount())
            << "an upload over a scenario-less configuration reported something: " << lastAlert();
}

TEST_F(AutoScenarioUploadGuardTest, AnOrdinaryStartupWithNoUploadBehindItSaysNothing)
{
    /* ✅ PROVE, DO NOT FLIP. The overwhelmingly common case: the server boots,
     * nobody uploaded anything, and the two scenarios are simply there. This
     * is the witness that the whole level 2 machinery is inert outside the
     * upload path - it is what makes it safe to run at every startup.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    const size_t alertsBefore = alertCount();
    rebootOnDiskConfig();

    ASSERT_EQ(3u, stepCountOf(eveningId));
    ASSERT_EQ(1u, stepCountOf(wakeupId));
    EXPECT_EQ(alertsBefore, alertCount())
            << "an ordinary startup reported something: " << lastAlert();
}

/*******************************************************************************
 * SECTION 4 - ⛔ NO REFUSAL, EVER
 *
 * D10 level 3 is ruled out by decision (E4.6.md §4). Deleting a scenario from
 * calaos_installer is a legitimate thing to do, and a server that refuses an
 * upload because something it used to know is not in it any more makes that
 * impossible. This case is the standing proof, and it must stay green through
 * every future hardening of this path.
 ******************************************************************************/

TEST_F(AutoScenarioUploadGuardTest, TheUploadThatLegitimatelyDeletesAScenarioIsAcceptedAndApplied)
{
    /* ✅ PROVE, DO NOT FLIP. Three things are measured, because "accepted" is
     * not one property: the reply says success, the bytes really landed on
     * disk, and the deletion really took effect after the restart.
     */
    loadHouse();

    std::string eveningId, wakeupId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        wakeupId = createWakeupScenario(ws);
    }
    saveConfig();

    std::string ioXml = ioXmlOnDisk();
    std::string rulesXml = rulesXmlOnDisk();
    const std::string marker = markerOf(ioXml, eveningId);
    ASSERT_GT(removeScenarioFromIoXml(ioXml, marker), 0);
    ASSERT_GT(removeScenarioFromRulesXml(rulesXml, marker), 0);

    const Json reply = uploadConfig(ioXml, rulesXml);
    EXPECT_EQ("true", str(reply, "success")) << reply.dump();
    EXPECT_TRUE(reply.find("error_str") == reply.cend()) << reply.dump();

    //the bytes landed, verbatim
    EXPECT_EQ(ioXml, ioXmlOnDisk());
    EXPECT_EQ(rulesXml, rulesXmlOnDisk());

    rebootOnDiskConfig();

    EXPECT_TRUE(scenarioIo(eveningId) == nullptr) << "the deletion was undone";
    ASSERT_TRUE(scenarioIo(wakeupId) != nullptr);
    EXPECT_EQ(1u, stepCountOf(wakeupId));
    EXPECT_EQ(1u, ListeRoom::Instance().getAutoScenarios().size());
}

/******************************************************************************
 * What stops a put, and what a put stops
 ******************************************************************************/

//A plain file where the folder goes makes the backup fail, even for the root user of CI
TEST_F(AutoScenarioUploadGuardTest, APutWhoseBackupFailsWritesNothingAndDoesNotFreeze)
{
    loadHouse();
    saveConfig();
    const std::string ioBefore = ioXmlOnDisk();
    const std::string rulesBefore = rulesXmlOnDisk();

    const std::string backups = configDir() + "/backups";
    std::filesystem::remove_all(backups);
    std::ofstream(backups) << "not a folder\n";

    const Json answer = uploadConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n", "<?xml version=\"1.0\"?>\n<calaos:rules/>\n");

    EXPECT_EQ("false", answer.value("success", ""));
    EXPECT_EQ("Could not copy the current files to backups, nothing was written", answer.value("error", ""));
    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
    EXPECT_FALSE(Utils::config_frozen());

    std::filesystem::remove(backups);
}

TEST_F(AutoScenarioUploadGuardTest, APutHoldsBackEveryWriteUntilTheRestart)
{
    loadHouse();
    saveConfig();

    const std::string ioXml = "<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n";
    const std::string rulesXml = "<?xml version=\"1.0\"?>\n<calaos:rules/>\n";
    ASSERT_EQ("true", uploadConfig(ioXml, rulesXml).value("success", ""));
    ASSERT_TRUE(Utils::config_frozen());

    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();
    EXPECT_FALSE(Utils::set_config_option("smtp_server", "late.example.org"));
    EXPECT_EQ(ioXml, ioXmlOnDisk());
    EXPECT_EQ(rulesXml, rulesXmlOnDisk());

    const size_t backups = backupsOf("io.xml").size();
    const Json second = uploadConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n", "<?xml version=\"1.0\"?>\n<calaos:rules/>\n");
    EXPECT_EQ("false", second.value("success", ""));
    EXPECT_EQ("The configuration is frozen until the restart", second.value("error", ""));
    EXPECT_EQ(ioXml, ioXmlOnDisk());
    EXPECT_EQ(rulesXml, rulesXmlOnDisk());
    EXPECT_EQ(backups, backupsOf("io.xml").size());
}

TEST_F(AutoScenarioUploadGuardTest, APutCannotOverwriteARestoreUnderWay)
{
    loadHouse();
    saveConfig();
    const std::string ioBefore = ioXmlOnDisk();
    const std::string rulesBefore = rulesXmlOnDisk();

    Utils::set_config_frozen(true);
    const Json answer = uploadConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n", "<?xml version=\"1.0\"?>\n<calaos:rules/>\n");

    EXPECT_EQ("false", answer.value("success", ""));
    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
    EXPECT_TRUE(backupsOf("io.xml").empty());
}

TEST_F(AutoScenarioUploadGuardTest, APutRestartsEvenIfItsClientHasGone)
{
    loadHouse();
    saveConfig();
    letEarlierRestartTimersExpire();

    ASSERT_EQ("true", uploadConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n", "<?xml version=\"1.0\"?>\n<calaos:rules/>\n").value("success", ""));
    ASSERT_TRUE(Utils::config_frozen());

    EXPECT_TRUE(loopStopsWithin(3000)) << "frozen with no restart on the way";
}

TEST_F(AutoScenarioUploadGuardTest, ARefusedPutDoesNotRestart)
{
    loadHouse();
    saveConfig();
    letEarlierRestartTimersExpire();

    Utils::set_config_frozen(true);
    ASSERT_EQ("false", uploadConfig("<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n", "<?xml version=\"1.0\"?>\n<calaos:rules/>\n").value("success", ""));

    EXPECT_FALSE(loopStopsWithin(800));
}

/******************************************************************************
 * A config file that did not load: a read writes nothing, a change is refused
 * and a put is the way out
 ******************************************************************************/

class UnloadedConfigFileTest: public AutoScenarioUploadGuardTest
{
protected:
    static constexpr const char *kGarbage = "live file garbage <<<";
    static constexpr mode_t kMode = 0640;

    std::string pathOf(const char *name) const { return configDir() + "/" + name; }

    static mode_t modeOf(const std::string &path)
    {
        struct stat st;
        EXPECT_EQ(0, ::stat(path.c_str(), &st)) << path;
        return st.st_mode & 07777;
    }

    //The server starts again on a file it cannot read, left on disk as it is
    void restartOnGarbage(const char *name)
    {
        {
            std::ofstream f(pathOf(name), std::ios::trunc);
            f << kGarbage;
        }
        ASSERT_EQ(0, ::chmod(pathOf(name).c_str(), kMode));
        reloadFromDisk();
    }

    void expectGarbageUntouched(const char *name) const
    {
        EXPECT_EQ(kGarbage, readWholeFile(pathOf(name)));
        EXPECT_EQ(kMode, modeOf(pathOf(name)));
        EXPECT_FALSE(std::filesystem::exists(pathOf(name) + ".restore"));
    }

    Json configGet()
    {
        HttpTestRequest req;
        req.send(authenticated(Json{ { "action", "config" }, { "type", "get" } }));
        EXPECT_EQ(1u, req.count());
        if (req.count() != 1) return Json::object();
        return req.bodyJson();
    }

    Json wsSetTimerange(WsTestSession &ws, const std::string &id)
    {
        ws.clear();
        ws.send(Json{ { "msg", "set_timerange" }, { "msg_id", "1" }, { "data", { { "id", id }, { "months", "011111111111" } } } });
        EXPECT_EQ(1u, ws.count());
        if (ws.count() != 1) return Json::object();
        return ws.lastData();
    }

    static Json createdScenario()
    {
        return Json{ { "type", "create" },
                     { "name", "Added" },
                     { "room_name", ROOM_NAME_E46H },
                     { "room_type", ROOM_TYPE_E46H },
                     { "steps", oneStep() } };
    }

    static Json modifiedScenario(const std::string &id)
    {
        return Json{ { "type", "modify" },
                     { "id", id },
                     { "name", "Renamed" },
                     { "room_name", ROOM_NAME_E46H },
                     { "room_type", ROOM_TYPE_E46H },
                     { "steps", oneStep() } };
    }

private:
    static Json oneStep()
    {
        return Json::array({ Json{ { "pause", "1" },
                                   { "actions", Json::array({ Json{ { "io", IO_LAMP }, { "value", "true" } } }) } } });
    }
};

TEST_F(UnloadedConfigFileTest, EveryChangeThatTouchesAnUnloadedRulesFileIsRefused)
{
    loadHouse();
    std::string eveningId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        ASSERT_FALSE(eveningId.empty());
        ASSERT_FALSE(wsAutoscenario(ws, Json{ { "type", "add_schedule" }, { "id", eveningId } }).contains("error"));
    }
    saveConfig();
    const std::string ioBefore = ioXmlOnDisk();
    restartOnGarbage(RULES_CONFIG);
    ASSERT_TRUE(Config::Instance().ioFileLoaded());
    ASSERT_FALSE(Config::Instance().rulesFileLoaded());

    WsTestSession ws;
    const Json changes[] = { createdScenario(),
                             modifiedScenario(eveningId),
                             Json{ { "type", "delete" }, { "id", eveningId } },
                             Json{ { "type", "del_schedule" }, { "id", eveningId } },
                             Json{ { "type", "add_schedule" }, { "id", eveningId } } };
    const size_t scenarios = ListeRoom::Instance().getAutoScenarios().size();
    for (const Json &change: changes)
        EXPECT_EQ("{\"error\":\"config_not_loaded\"}", wsAutoscenario(ws, change).dump()) << change.dump();

    //Nothing changed in memory either: the answer is not a lie
    EXPECT_EQ(scenarios, ListeRoom::Instance().getAutoScenarios().size());
    ASSERT_NE(nullptr, scenarioIo(eveningId));
    EXPECT_EQ(3u, stepCountOf(eveningId));
    EXPECT_EQ(NAME_EVENING, scenarioIo(eveningId)->get_param("name"));
    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    expectGarbageUntouched(RULES_CONFIG);
}

TEST_F(UnloadedConfigFileTest, NoChangeReachesAnUnloadedIoFile)
{
    loadHouse();
    saveConfig();
    const std::string rulesBefore = rulesXmlOnDisk();
    restartOnGarbage(IO_CONFIG);
    ASSERT_FALSE(Config::Instance().ioFileLoaded());
    ASSERT_TRUE(Config::Instance().rulesFileLoaded());

    WsTestSession ws;
    //The room went with the file
    EXPECT_NE(std::string::npos, wsAutoscenario(ws, createdScenario()).value("error", "").find("no room"));
    const std::string unknown = "gone_with_the_file";
    const Json changes[] = { modifiedScenario(unknown),
                             Json{ { "type", "delete" }, { "id", unknown } },
                             Json{ { "type", "add_schedule" }, { "id", unknown } },
                             Json{ { "type", "del_schedule" }, { "id", unknown } },
                             Json{ { "type", "reenable" }, { "id", unknown } } };
    for (const Json &change: changes)
        EXPECT_EQ("{\"error\":\"wrong input\"}", wsAutoscenario(ws, change).dump()) << change.dump();
    EXPECT_EQ("wrong input", wsSetTimerange(ws, unknown).value("error", ""));

    expectGarbageUntouched(IO_CONFIG);
    EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
}

//Those live in io.xml alone: nothing is lost by leaving rules.xml out
TEST_F(UnloadedConfigFileTest, WhatOnlyLivesInIoXmlStillGoesThrough)
{
    loadHouse();
    std::string eveningId;
    std::string scheduleId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        ASSERT_FALSE(eveningId.empty());
        scheduleId = wsAutoscenario(ws, Json{ { "type", "add_schedule" }, { "id", eveningId } }).value("id", "");
        ASSERT_FALSE(scheduleId.empty());
    }
    saveConfig();
    const std::string ioBefore = ioXmlOnDisk();
    restartOnGarbage(RULES_CONFIG);

    WsTestSession ws;
    EXPECT_EQ("true", wsSetTimerange(ws, scheduleId).value("success", ""));
    EXPECT_EQ("true", wsAutoscenario(ws, Json{ { "type", "reenable" }, { "id", eveningId } }).value("success", ""));

    EXPECT_NE(ioBefore, ioXmlOnDisk());
    expectGarbageUntouched(RULES_CONFIG);
}

TEST_F(UnloadedConfigFileTest, AConfigGetHandsBackWhatIsOnDiskAndWritesNothing)
{
    loadHouse();
    saveConfig();
    const std::string goodRules = rulesXmlOnDisk();
    restartOnGarbage(IO_CONFIG);

    const Json reply = configGet();

    EXPECT_EQ("true", reply.value("success", ""));
    EXPECT_EQ(kGarbage, reply["config_files"].value("io.xml", ""));
    EXPECT_EQ(goodRules, reply["config_files"].value("rules.xml", ""));
    expectGarbageUntouched(IO_CONFIG);
}

TEST_F(UnloadedConfigFileTest, AConfigGetLeavesAnUnloadedRulesFileToo)
{
    loadHouse();
    saveConfig();
    restartOnGarbage(RULES_CONFIG);

    EXPECT_EQ(kGarbage, configGet()["config_files"].value("rules.xml", ""));
    expectGarbageUntouched(RULES_CONFIG);
}

TEST_F(UnloadedConfigFileTest, AConfigGetSurvivesADirectoryInPlaceOfTheFile)
{
    loadHouse();
    saveConfig();
    ASSERT_EQ(0, ::unlink(pathOf(IO_CONFIG).c_str()));
    ASSERT_TRUE(std::filesystem::create_directory(pathOf(IO_CONFIG)));
    reloadFromDisk();
    ASSERT_FALSE(Config::Instance().ioFileLoaded());

    const Json reply = configGet();

    EXPECT_EQ("true", reply.value("success", ""));
    EXPECT_EQ("", reply["config_files"].value("io.xml", "not there"));
    EXPECT_TRUE(std::filesystem::is_directory(pathOf(IO_CONFIG)));
    ::rmdir(pathOf(IO_CONFIG).c_str());
}

TEST_F(UnloadedConfigFileTest, AConfigPutIsTheWayOutAndTheRestartLoadsItNormally)
{
    loadHouse();
    std::string eveningId;
    {
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
    }
    ASSERT_FALSE(eveningId.empty());
    saveConfig();
    const std::string ioXml = ioXmlOnDisk();
    const std::string rulesXml = rulesXmlOnDisk();
    restartOnGarbage(IO_CONFIG);
    restartOnGarbage(RULES_CONFIG);
    ASSERT_FALSE(Config::Instance().ioFileLoaded());
    ASSERT_FALSE(Config::Instance().rulesFileLoaded());
    letEarlierRestartTimersExpire();

    EXPECT_EQ("true", uploadConfig(ioXml, rulesXml).value("success", ""));

    EXPECT_EQ(ioXml, ioXmlOnDisk());
    EXPECT_EQ(rulesXml, rulesXmlOnDisk());
    EXPECT_EQ(kMode, modeOf(pathOf(IO_CONFIG)));
    EXPECT_EQ(kMode, modeOf(pathOf(RULES_CONFIG)));

    Utils::set_config_frozen(false);
    reloadFromDisk();

    EXPECT_TRUE(Config::Instance().ioFileLoaded());
    EXPECT_TRUE(Config::Instance().rulesFileLoaded());
    ASSERT_NE(nullptr, scenarioIo(eveningId));
    EXPECT_EQ(3u, stepCountOf(eveningId));

    //And the saves are back
    WsTestSession ws;
    EXPECT_EQ("true", wsAutoscenario(ws, Json{ { "type", "delete" }, { "id", eveningId } }).value("success", ""));
    EXPECT_NE(ioXml, ioXmlOnDisk());
}

/******************************************************************************
 * A change that did not reach its file: the answer owns up to it
 ******************************************************************************/

class UnwrittenConfigFileTest: public UnloadedConfigFileTest
{
protected:
    enum class Transport
    {
        WebSocket,
        Http,
    };

    static constexpr const char *kNotWritten = "{\"error\":\"config_not_written\"}";
    static constexpr const char *kNotLoaded = "{\"error\":\"config_not_loaded\"}";
    static constexpr const char *kCanary = "canary only the file holds";

    //In the way of the temporary of the atomic write: stops a root process too,
    //and one file at a time
    void blockWritesTo(const char *name) const
    {
        ASSERT_TRUE(std::filesystem::create_directory(pathOf(name) + ".restore"));
    }

    void unblockWritesTo(const char *name) const
    {
        ASSERT_TRUE(std::filesystem::remove(pathOf(name) + ".restore"));
    }

    void blockBothFiles() const
    {
        blockWritesTo(IO_CONFIG);
        blockWritesTo(RULES_CONFIG);
    }

    void unblockBothFiles() const
    {
        unblockWritesTo(IO_CONFIG);
        unblockWritesTo(RULES_CONFIG);
    }

    void houseOnDisk()
    {
        loadHouse();
        WsTestSession ws;
        eveningId = createEveningScenario(ws);
        ASSERT_FALSE(eveningId.empty());
        scheduleId = wsAutoscenario(ws, Json{ { "type", "add_schedule" }, { "id", eveningId } }).value("id", "");
        ASSERT_FALSE(scheduleId.empty());
        ListeRoom::Instance().get_io(IO_LAMP)->set_param("canary", kCanary);
        saveConfig();
        ASSERT_NE(std::string::npos, ioXmlOnDisk().find(kCanary));
        ioBefore = ioXmlOnDisk();
        rulesBefore = rulesXmlOnDisk();
    }

    Json timerange() const
    {
        return Json{ { "id", scheduleId }, { "months", "011111111111" } };
    }

    Json reenable() const
    {
        return Json{ { "type", "reenable" }, { "id", eveningId } };
    }

    std::vector<Json> everyChange() const
    {
        return { timerange(),
                 createdScenario(),
                 modifiedScenario(eveningId),
                 Json{ { "type", "add_schedule" }, { "id", eveningId } },
                 Json{ { "type", "del_schedule" }, { "id", eveningId } },
                 Json{ { "type", "delete" }, { "id", eveningId } },
                 reenable() };
    }

    Json answerTo(Transport transport, const Json &change)
    {
        const bool scenario = change.contains("type");
        if (transport == Transport::WebSocket)
        {
            WsTestSession ws;
            return scenario ? wsAutoscenario(ws, change) : wsSetTimerange(ws, change.value("id", ""));
        }

        Json request = change;
        request["action"] = scenario ? "autoscenario" : "set_timerange";
        HttpTestRequest req;
        req.send(authenticated(request));
        EXPECT_EQ(1u, req.count());
        if (req.count() != 1) return Json::object();
        return req.bodyJson();
    }

    //Same answer as one change, plus what it logged
    Json answerTo(Transport transport, const Json &change, std::string &logged)
    {
        testing::internal::CaptureStdout();
        const Json answer = answerTo(transport, change);
        logged = testing::internal::GetCapturedStdout();
        return answer;
    }

    static void expectNoFileContentIn(const std::string &logged)
    {
        EXPECT_EQ(std::string::npos, logged.find(kCanary)) << logged;
        EXPECT_EQ(std::string::npos, logged.find("<calaos:")) << logged;
    }

    std::string eveningId;
    std::string scheduleId;
    std::string ioBefore;
    std::string rulesBefore;
};

TEST_F(UnwrittenConfigFileTest, EveryChangeThatReachesNoFileSaysSoOnBothTransports)
{
    houseOnDisk();
    const size_t changes = everyChange().size();

    for (Transport transport: { Transport::WebSocket, Transport::Http })
    {
        for (size_t i = 0; i < changes; i++)
        {
            const Json change = everyChange()[i];
            SCOPED_TRACE(change.dump());
            blockBothFiles();

            std::string logged;
            EXPECT_EQ(kNotWritten, answerTo(transport, change, logged).dump());

            EXPECT_EQ(ioBefore, ioXmlOnDisk());
            EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
            EXPECT_NE(std::string::npos, logged.find("Unable to save")) << logged;
            expectNoFileContentIn(logged);

            unblockBothFiles();
            reloadFromDisk();
        }
    }
}

TEST_F(UnwrittenConfigFileTest, AFailedIoFileStillLetsTheRulesFileBeWritten)
{
    houseOnDisk();
    blockWritesTo(IO_CONFIG);

    std::string logged;
    EXPECT_EQ(kNotWritten, answerTo(Transport::Http, Json{ { "type", "delete" }, { "id", eveningId } }, logged).dump());

    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    EXPECT_NE(rulesBefore, rulesXmlOnDisk());
    EXPECT_NE(std::string::npos, logged.find(IO_CONFIG)) << logged;
    EXPECT_EQ(std::string::npos, logged.find("Unable to save " + pathOf(RULES_CONFIG))) << logged;
    expectNoFileContentIn(logged);
    unblockWritesTo(IO_CONFIG);
}

TEST_F(UnwrittenConfigFileTest, AFailedRulesFileIsAnsweredOnceTheIoFileIsWritten)
{
    houseOnDisk();
    blockWritesTo(RULES_CONFIG);

    std::string logged;
    EXPECT_EQ(kNotWritten, answerTo(Transport::Http, Json{ { "type", "delete" }, { "id", eveningId } }, logged).dump());

    EXPECT_NE(ioBefore, ioXmlOnDisk());
    EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
    EXPECT_NE(std::string::npos, logged.find(RULES_CONFIG)) << logged;
    expectNoFileContentIn(logged);
    unblockWritesTo(RULES_CONFIG);
}

TEST_F(UnwrittenConfigFileTest, ReenableAnswersForTheOnlyFileItWrites)
{
    houseOnDisk();
    blockWritesTo(RULES_CONFIG);

    EXPECT_EQ("true", answerTo(Transport::WebSocket, reenable()).value("success", ""));
    EXPECT_EQ("true", answerTo(Transport::Http, reenable()).value("success", ""));

    unblockWritesTo(RULES_CONFIG);
}

TEST_F(UnwrittenConfigFileTest, TheChangeHoldsInMemoryUntilASaveCarriesItOrARestartDropsIt)
{
    houseOnDisk();
    const size_t scenarios = ListeRoom::Instance().getAutoScenarios().size();
    blockBothFiles();

    EXPECT_EQ(kNotWritten, answerTo(Transport::WebSocket, createdScenario()).dump());
    EXPECT_EQ(scenarios + 1, ListeRoom::Instance().getAutoScenarios().size());

    //The error does not tell a retry that the first one holds: it adds another
    EXPECT_EQ(kNotWritten, answerTo(Transport::WebSocket, createdScenario()).dump());
    EXPECT_EQ(scenarios + 2, ListeRoom::Instance().getAutoScenarios().size());

    EXPECT_EQ(kNotWritten, answerTo(Transport::WebSocket, modifiedScenario(eveningId)).dump());
    EXPECT_EQ("Renamed", scenarioIo(eveningId)->get_param("name"));

    EXPECT_EQ(kNotWritten, answerTo(Transport::WebSocket, Json{ { "type", "delete" }, { "id", eveningId } }).dump());
    EXPECT_EQ(nullptr, scenarioIo(eveningId));
    EXPECT_EQ("{\"error\":\"wrong input\"}",
              answerTo(Transport::WebSocket, Json{ { "type", "delete" }, { "id", eveningId } }).dump());

    unblockBothFiles();
    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    EXPECT_EQ(rulesBefore, rulesXmlOnDisk());

    //A restart before any save goes back to the files...
    reloadFromDisk();
    ASSERT_NE(nullptr, scenarioIo(eveningId));
    EXPECT_EQ(NAME_EVENING, scenarioIo(eveningId)->get_param("name"));
    EXPECT_EQ(scenarios, ListeRoom::Instance().getAutoScenarios().size());

    //...and any later save carries what memory held
    blockBothFiles();
    EXPECT_EQ(kNotWritten, answerTo(Transport::Http, createdScenario()).dump());
    unblockBothFiles();
    EXPECT_EQ("true", answerTo(Transport::Http, timerange()).value("success", ""));
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("Added"));
    reloadFromDisk();
    EXPECT_EQ(scenarios + 1, ListeRoom::Instance().getAutoScenarios().size());
    EXPECT_FALSE(std::filesystem::exists(pathOf(IO_CONFIG) + ".restore"));
    EXPECT_FALSE(std::filesystem::exists(pathOf(RULES_CONFIG) + ".restore"));
}

TEST_F(UnwrittenConfigFileTest, TheFreezeIsAnsweredTheSameAndLogsNoFailure)
{
    houseOnDisk();
    const size_t changes = everyChange().size();

    for (Transport transport: { Transport::WebSocket, Transport::Http })
    {
        for (size_t i = 0; i < changes; i++)
        {
            const Json change = everyChange()[i];
            SCOPED_TRACE(change.dump());
            Utils::set_config_frozen(true);

            std::string logged;
            EXPECT_EQ(kNotWritten, answerTo(transport, change, logged).dump());

            EXPECT_EQ(ioBefore, ioXmlOnDisk());
            EXPECT_EQ(rulesBefore, rulesXmlOnDisk());
            EXPECT_EQ(std::string::npos, logged.find("Unable to save")) << logged;

            Utils::set_config_frozen(false);
            reloadFromDisk();
        }
    }
}

TEST_F(UnwrittenConfigFileTest, AnUnloadedRulesFileHidesNeitherAFailedIoFileNorItsOwnRefusal)
{
    houseOnDisk();
    restartOnGarbage(RULES_CONFIG);
    blockWritesTo(IO_CONFIG);

    EXPECT_EQ(kNotWritten, answerTo(Transport::WebSocket, timerange()).dump());
    EXPECT_EQ(kNotWritten, answerTo(Transport::Http, reenable()).dump());

    EXPECT_EQ(kNotLoaded, answerTo(Transport::WebSocket, createdScenario()).dump());
    EXPECT_EQ(kNotLoaded, answerTo(Transport::Http, createdScenario()).dump());

    EXPECT_EQ(ioBefore, ioXmlOnDisk());
    unblockWritesTo(IO_CONFIG);
    expectGarbageUntouched(RULES_CONFIG);
}

TEST_F(UnwrittenConfigFileTest, AConfigGetAnswersTheSameOverAFailedSave)
{
    houseOnDisk();
    const Json before = configGet();
    ASSERT_EQ(ioBefore, before["config_files"].value("io.xml", ""));
    ListeRoom::Instance().get_io(IO_LAMP)->set_param("canary", "only in memory");
    blockBothFiles();

    testing::internal::CaptureStdout();
    const Json reply = configGet();
    const std::string logged = testing::internal::GetCapturedStdout();

    EXPECT_EQ(before.dump(), reply.dump());
    EXPECT_NE(std::string::npos, logged.find("Unable to save")) << logged;
    expectNoFileContentIn(logged);
    unblockBothFiles();
}

/******************************************************************************
 * config put: the content is judged before any write, and the write is atomic
 ******************************************************************************/

class ConfigPutTest: public UnloadedConfigFileTest
{
protected:
    static constexpr const char *kIoRoot = "<?xml version=\"1.0\"?>\n<calaos:ioconfig/>\n";
    static constexpr const char *kRulesRoot = "<?xml version=\"1.0\"?>\n<calaos:rules/>\n";

    Json put(const Json &files)
    {
        HttpTestRequest req;
        req.send(authenticated(Json{ { "action", "config" }, { "type", "put" }, { "config_files", files } }));
        EXPECT_EQ(1u, req.count());
        if (req.count() != 1) return Json::object();
        return req.bodyJson();
    }

    //Both files in place with a mode that is not the umask's, nothing stored yet
    void settle()
    {
        loadHouse();
        saveConfig();
        ASSERT_EQ(0, ::chmod(pathOf(IO_CONFIG).c_str(), 0600));
        ASSERT_EQ(0, ::chmod(pathOf(RULES_CONFIG).c_str(), 0600));
        letEarlierRestartTimersExpire();
    }

    void expectRefused(const Json &files, const std::string &reason)
    {
        const std::string ioBefore = readWholeFile(pathOf(IO_CONFIG));
        const std::string rulesBefore = readWholeFile(pathOf(RULES_CONFIG));
        const size_t backups = backupsOf("io.xml").size();

        const Json reply = put(files);

        EXPECT_EQ("false", reply.value("success", ""));
        EXPECT_NE(std::string::npos, reply.value("error", "").find(reason)) << reply.dump();
        EXPECT_EQ(ioBefore, readWholeFile(pathOf(IO_CONFIG)));
        EXPECT_EQ(rulesBefore, readWholeFile(pathOf(RULES_CONFIG)));
        EXPECT_EQ(0600, modeOf(pathOf(IO_CONFIG)));
        EXPECT_EQ(backups, backupsOf("io.xml").size());
        EXPECT_FALSE(Utils::config_frozen());
    }

    std::string blockRulesWrite();

    std::vector<std::string> strays() const
    {
        std::vector<std::string> res;
        for (const auto &e: std::filesystem::directory_iterator(configDir()))
        {
            const std::string name = e.path().filename().string();
            if (name.find(".restore") != std::string::npos || name.rfind(".io.xml.", 0) == 0 ||
                name.rfind(".rules.xml.", 0) == 0)
                res.push_back(name);
        }
        return res;
    }
};

TEST_F(ConfigPutTest, AWrongRootIsRefusedWithTheFilesAndTheBackupsUntouched)
{
    settle();
    expectRefused(Json{ { "io.xml", "<?xml version=\"1.0\"?>\n<first/>\n" }, { "rules.xml", kRulesRoot } },
                  "io.xml does not have the root element calaos:ioconfig");
    expectRefused(Json{ { "io.xml", kIoRoot }, { "rules.xml", kIoRoot } },
                  "rules.xml does not have the root element calaos:rules");
}

TEST_F(ConfigPutTest, ABrokenOrEmptyContentIsRefusedWithTheFilesAndTheBackupsUntouched)
{
    settle();
    expectRefused(Json{ { "io.xml", "<?xml version=\"1.0\"?><calaos:ioconfig><calaos:home" } }, "io.xml is not valid XML");
    expectRefused(Json{ { "rules.xml", "" } }, "rules.xml does not start with <?xml");
    expectRefused(Json{ { "io.xml", kIoRoot }, { "local_config.xml", "not xml" } },
                  "local_config.xml does not start with <?xml");
}

TEST_F(ConfigPutTest, AValidPutOverACorruptedFileIsAccepted)
{
    settle();
    restartOnGarbage(IO_CONFIG);
    ASSERT_FALSE(Config::Instance().ioFileLoaded());

    EXPECT_EQ("true", put(Json{ { "io.xml", kIoRoot } }).value("success", ""));
    EXPECT_EQ(kIoRoot, readWholeFile(pathOf(IO_CONFIG)));
    EXPECT_EQ(kMode, modeOf(pathOf(IO_CONFIG)));

    Utils::set_config_frozen(false);
    reloadFromDisk();
    EXPECT_TRUE(Config::Instance().ioFileLoaded());
    EXPECT_TRUE(strays().empty());
}

TEST_F(ConfigPutTest, AnUnknownNameOrANonStringValueIsRefusedBeforeAnyBackupOrWrite)
{
    settle();
    expectRefused(Json{ { "io.xml", 123 } }, "io.xml is not a string");
    expectRefused(Json{ { "foo.xml", "x" }, { "io.xml", kIoRoot } }, "foo.xml is not a valid config filename");
    expectRefused(Json{ { "io.xml", kIoRoot }, { "rules.xml", nullptr } }, "rules.xml is not a string");
}

//Judging a name is the caller's job (the handler refuses it first); the content
//check only knows the three config files.
TEST_F(ConfigPutTest, TheContentCheckLeavesAnUnknownNameToTheCaller)
{
    EXPECT_TRUE(AdminConfig::putFileRefusal("foo.xml", "not xml").empty());
    EXPECT_FALSE(AdminConfig::putFileRefusal("io.xml", "not xml").empty());
}

//A link where the temporary of rules.xml goes: it is opened without following
//links, so the write fails for the root user of CI too. Returns the link.
std::string ConfigPutTest::blockRulesWrite()
{
    const std::string blocker = pathOf(RULES_CONFIG) + ".restore";
    EXPECT_EQ(0, ::symlink("/nonexistent", blocker.c_str()));
    return blocker;
}

TEST_F(ConfigPutTest, AWriteThatFailsLeavesTheLiveFileAndNoTemporary)
{
    settle();
    const std::string before = readWholeFile(pathOf(RULES_CONFIG));
    const std::string blocker = blockRulesWrite();

    const Json reply = put(Json{ { "rules.xml", kRulesRoot } });
    ::unlink(blocker.c_str());

    EXPECT_EQ("false", reply.value("success", ""));
    EXPECT_EQ("Could not write rules.xml, the file was left as it was", reply.value("error", "")) << reply.dump();
    EXPECT_EQ(before, readWholeFile(pathOf(RULES_CONFIG)));
    EXPECT_TRUE(strays().empty());
    EXPECT_FALSE(Utils::config_frozen());
}

TEST_F(ConfigPutTest, AWriteThatFailsAfterAnotherFileWasReplacedFreezesAndNamesThem)
{
    settle();
    const std::string before = readWholeFile(pathOf(RULES_CONFIG));
    const std::string blocker = blockRulesWrite();

    const Json reply = put(Json{ { "io.xml", kIoRoot }, { "rules.xml", kRulesRoot } });
    ::unlink(blocker.c_str());

    const std::string error = reply.value("error", "");
    EXPECT_EQ("false", reply.value("success", ""));
    EXPECT_EQ(0u, error.find("Could not write rules.xml; already replaced: io.xml; previous files in backups/")) << error;
    EXPECT_EQ(kIoRoot, readWholeFile(pathOf(IO_CONFIG)));
    EXPECT_EQ(before, readWholeFile(pathOf(RULES_CONFIG)));
    EXPECT_TRUE(strays().empty());
    //Memory and disk now differ: nothing may save over the new file
    EXPECT_TRUE(Utils::config_frozen());
}

TEST_F(ConfigPutTest, ThePutKeepsTheModeAndFollowsTheLink)
{
    settle();
    const std::string real = configDir() + "/real_rules.xml";
    ASSERT_EQ(0, ::rename(pathOf(RULES_CONFIG).c_str(), real.c_str()));
    ASSERT_EQ(0, ::chmod(real.c_str(), 0640));
    ASSERT_EQ(0, ::symlink(real.c_str(), pathOf(RULES_CONFIG).c_str()));

    EXPECT_EQ("true", put(Json{ { "rules.xml", kRulesRoot } }).value("success", ""));

    EXPECT_TRUE(std::filesystem::is_symlink(pathOf(RULES_CONFIG)));
    EXPECT_EQ(kRulesRoot, readWholeFile(real));
    EXPECT_EQ(0640, modeOf(real));
    EXPECT_TRUE(strays().empty());
    ::unlink(pathOf(RULES_CONFIG).c_str());
    ::unlink(real.c_str());
}
