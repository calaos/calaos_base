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
 * A successful `config put` sets need_restart, and HttpClient::DataWritten()
 * stops the event loop as soon as the reply is out (HttpClient.cpp:476-481).
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

#include "AutoScenario.h"
#include "AutoScenarioDef.h"
#include "CalaosConfig.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Scenario.h"

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

    //second upload: everything goes
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

    alignToNextSecond();
    const std::string stamp = backupSecondStamp();

    //first upload: the evening scenario goes. Second: everything goes. No wait
    //between them - that is the whole point of the case.
    ASSERT_EQ("true", str(uploadConfig(ioOnce, rulesOnce), "success"));
    ASSERT_EQ("true", str(uploadConfig(ioXmlDocument(roomXml("Vide", "salon", std::string(), 0)),
                                       rulesXmlDocument(std::string())), "success"));

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
