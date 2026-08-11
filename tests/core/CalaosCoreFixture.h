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
#ifndef CALAOS_CORE_FIXTURE_H
#define CALAOS_CORE_FIXTURE_H

/******************************************************************************
 * Reusable GTest scaffolding for the calaos_server core
 * =====================================================
 *
 * This is the shared harness for everything that lives around the core data
 * model: IOBase, Room, ListeRoom, IOFactory, Rule, ListeRule, Condition*,
 * Action* and Config (CalaosConfig.h). It is meant to be #included by every
 * core test so that none of them has to redo the config-directory dance or
 * the singleton clean up.
 *
 *   #include "CalaosCoreFixture.h"
 *
 *   class MyTest: public CalaosTest::CoreFixture {};
 *
 *   TEST_F(MyTest, Something)
 *   {
 *       loadConfig();                              //default minimal config
 *       ASSERT_TRUE(io(ID_BOOL_IN) != nullptr);
 *   }
 *
 * ---------------------------------------------------------------------------
 * How the config is isolated
 * ---------------------------------------------------------------------------
 * Config (the singleton in CalaosConfig.h) has no API to load from a string:
 * LoadConfigIO()/LoadConfigRule() always read Utils::getConfigFile(IO_CONFIG)
 * and Utils::getConfigFile(RULES_CONFIG) from disk, and they call exit(-1) on
 * a parse error. The fixture therefore keeps the "in memory" feeling at the
 * API level - callers hand over XML *strings* - and does the disk part itself:
 *
 *   - SetUp() creates a private directory `<TMPDIR>/calaos_coretest_XXXXXX`
 *     with a `config/` and a `cache/` sub directory,
 *   - it points the library at them with Utils::initConfigOptions(), which is
 *     the only entry point resetting the cached `_configBase`/`_cacheBase` of
 *     the process (getConfigFile() honours CALAOS_CONFIG, and
 *     initConfigOptions() sets that variable too),
 *   - loadConfig(ioXml, rulesXml) writes the two strings there and calls
 *     Config::Instance().LoadConfigIO()/LoadConfigRule(),
 *   - TearDown() drops the whole tree.
 *
 * A test never has to touch a path: everything goes through loadConfig(),
 * saveConfig(), reloadFromDisk() and roundTripIo()/roundTripRules().
 *
 * ---------------------------------------------------------------------------
 * Singleton state: what is reset and what is NOT
 * ---------------------------------------------------------------------------
 * ListeRoom, ListeRule, Config, IOFactory, EventManager... are all process
 * wide singletons with no reset API. clearCoreState() (called by SetUp() and
 * TearDown()) empties what can be emptied through the public API:
 *
 *   - every Rule of ListeRule is removed and deleted,
 *   - every Room of ListeRoom is removed and deleted, which cascades into the
 *     deletion of its IOs, which unregisters them from ListeRoom::io_table.
 *
 * Known limitations, honestly:
 *
 *   - The IO *state cache* of Config (`cache_states`/`cache_params`, fed by
 *     SaveValueIO()/SaveValueParams() and read back by
 *     Internal::LoadFromConfig()) is loaded once, in the Config constructor,
 *     and there is no public way to clear it. A value written by a test for
 *     IO "x" is therefore still visible to the next test that creates an IO
 *     with the same id in the same process. Use distinct ids, or accept the
 *     inherited value. forgetIOState() below papers over the common case.
 *   - IOFactory's registry is filled by static initializers (REGISTER_IO), it
 *     is intentionally never reset - it only holds the IO types that are
 *     actually linked into the test binary (see tests/Makefile.am: the core
 *     tests deliberately link the internal IOs only, no hardware driver).
 *   - EventManager queues events into a libuv idler. No libuv loop runs in the
 *     tests, so the queue simply grows and no event is ever dispatched. This
 *     is harmless but it means event delivery cannot be observed here.
 *   - HistLogger/DataLogger are only reachable for IOs flagged with
 *     log_history="true"/logged="true". The minimal config sets neither, so no
 *     sqlite database and no influxdb request is ever created.
 *   - Because everything is a singleton, core tests must not be run with
 *     gtest_repeat/threads in parallel inside a single process.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>

#include "Calaos.h"
#include "CalaosConfig.h"
#include "IOBase.h"
#include "IOFactory.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "Params.h"
#include "Room.h"
#include "Rule.h"

namespace CalaosTest
{

/* Ids and names used by the default minimal configuration returned by
 * minimalIoXml()/minimalRulesXml(). They are exposed so that a test can refer
 * to them without hardcoding a string twice.
 */
extern const char *const ID_BOOL_IN;    //InternalBool, input of the default rule
extern const char *const ID_BOOL_OUT;   //InternalBool, output of the default rule
extern const char *const ID_INT;        //InternalInt
extern const char *const ID_STRING;     //InternalString
extern const char *const ROOM_NAME;     //name of the single room
extern const char *const ROOM_TYPE;     //type of the single room
extern const char *const RULE_NAME;     //name of the single rule

/* A minimal but *complete* io.xml: one room, three internal IOs. Internal IOs
 * are used on purpose, they have no hardware and no subprocess behind them, so
 * the core tests link a very small part of the server (see tests/Makefile.am).
 */
std::string minimalIoXml();

/* A minimal rules.xml: one rule "when ID_BOOL_IN becomes true, set ID_BOOL_OUT
 * to true". It only references ids declared by minimalIoXml().
 */
std::string minimalRulesXml();

/* Wrap a list of `<calaos:room>` elements into a valid io.xml document. Handy
 * to build a custom config without repeating the XML boilerplate:
 *
 *   ioXmlDocument(roomXml("Kitchen", "kitchen",
 *                         internalIoXml("InternalBool", "id_x", "X")));
 */
std::string ioXmlDocument(const std::string &roomsXml);
std::string roomXml(const std::string &name, const std::string &type,
                    const std::string &iosXml, int hits = 0);
std::string internalIoXml(const std::string &type, const std::string &id,
                          const std::string &name,
                          const std::string &extraAttributes = std::string());

/* Wrap a list of `<calaos:rule>` elements into a valid rules.xml document. */
std::string rulesXmlDocument(const std::string &rulesXml);

/* Build the XML of a one condition/one action rule:
 * "if <inputId> <oper> <inputValue> then <outputId> = <outputValue>".
 */
std::string simpleRuleXml(const std::string &name,
                          const std::string &inputId,
                          const std::string &oper,
                          const std::string &inputValue,
                          const std::string &outputId,
                          const std::string &outputValue,
                          const std::string &type = "rule");

/******************************************************************************
 * The fixture itself.
 ******************************************************************************/
class CoreFixture: public ::testing::Test
{
public:
    //Re-exported here so that TEST_F bodies can just write ID_BOOL_IN
    static const char *const ID_BOOL_IN;
    static const char *const ID_BOOL_OUT;
    static const char *const ID_INT;
    static const char *const ID_STRING;
    static const char *const ROOM_NAME;
    static const char *const ROOM_TYPE;
    static const char *const RULE_NAME;

protected:
    void SetUp() override;
    void TearDown() override;

    /* ---------------------------------------------------------------------
     * Config loading
     * ------------------------------------------------------------------ */

    /* Write the two XML documents into the private config directory and load
     * them through the real Config code path (IOFactory instantiation for the
     * IOs, RulesFactory for the conditions/actions).
     * Defaults to minimalIoXml()/minimalRulesXml().
     * Note: rules are loaded *after* the IOs, as the server does, because
     * ConditionStd/ActionStd resolve their ids against ListeRoom at load time
     * and silently drop the rule when an id is unknown.
     */
    void loadConfig(const std::string &ioXml = minimalIoXml(),
                    const std::string &rulesXml = minimalRulesXml());

    /* Same, but only writes the files - no load. Use it when the test wants to
     * observe what LoadConfigIO() does with a broken/partial document.
     */
    void writeConfig(const std::string &ioXml, const std::string &rulesXml);

    /* Config::SaveConfigIO()/SaveConfigRule() on the private directory. */
    void saveConfig();

    /* Forget everything in memory and load the files from disk again. */
    void reloadFromDisk();

    /* Raw content of the files as they are on disk right now. */
    std::string ioXmlOnDisk() const;
    std::string rulesXmlOnDisk() const;

    /* Directories in use. Only needed by tests that check paths themselves. */
    std::string configDir() const { return confDir; }
    std::string cacheDir() const { return cachDir; }

    /* ---------------------------------------------------------------------
     * State
     * ------------------------------------------------------------------ */

    /* Delete every Rule and every Room/IO held by the singletons. Called
     * automatically by SetUp() and TearDown(); exposed because a test doing
     * several load cycles needs it in the middle too.
     */
    static void clearCoreState();

    /* Overwrite the persistent value cached by Config for an IO id, so a value
     * saved by a previous test in the same binary cannot leak in (see the
     * limitation about the state cache at the top of this file).
     */
    static void forgetIOState(const std::string &id);

    /* ---------------------------------------------------------------------
     * XML round trip
     * ------------------------------------------------------------------ */

    /* Save everything, load it back, and check that nothing was lost on the
     * way. Two things are compared:
     *   - the structural dump (rooms, IOs and every parameter), and
     *   - the serialized io.xml, written once more after the reload, byte for
     *     byte, which also covers Config's own writing.
     * Returns a gtest AssertionResult, so a failure prints both states:
     *     EXPECT_TRUE(roundTripIo());
     * Both IOs and rules are saved and reloaded (a rule cannot be loaded
     * without its IOs), only the IOs are compared. Every object is recreated:
     * any IOBase/Room/Rule pointer held by the caller is dangling afterwards,
     * look them up again with io()/findRoom()/findRule().
     */
    ::testing::AssertionResult roundTripIo();

    /* Same full save/reload cycle, comparing the rules instead. Same warning
     * about dangling pointers.
     */
    ::testing::AssertionResult roundTripRules();

    /* Deterministic textual dumps, used by the round trip helpers and useful
     * on their own to compare two states in a test.
     */
    static std::string dumpIos();
    static std::string dumpRules();

    /* ---------------------------------------------------------------------
     * IO helpers
     * ------------------------------------------------------------------ */

    /* Shortcut for ListeRoom::Instance().get_io(id). */
    static Calaos::IOBase *io(const std::string &id);

    /* First room of ListeRoom, or the room with that name. nullptr if absent. */
    static Calaos::Room *firstRoom();
    static Calaos::Room *findRoom(const std::string &name);

    /* Create a room and register it in ListeRoom (ownership goes to it). */
    static Calaos::Room *addRoom(const std::string &name, const std::string &type);

    /* Create an IO through the real ListeRoom::createIO()/IOFactory path and
     * add it to `room` (the first room when null). Returns nullptr when the
     * type is not registered in this binary.
     */
    static Calaos::IOBase *createIO(Params params, Calaos::Room *room = nullptr);

    /* createIO() shortcut for the internal types linked in the core tests.
     * `type` is InternalBool, InternalInt or InternalString.
     */
    static Calaos::IOBase *createInternalIO(const std::string &type,
                                            const std::string &id,
                                            const std::string &name,
                                            Calaos::Room *room = nullptr);

    /* Delete an IO the way the JSON API does: drops the rules using it, then
     * removes it from its room and deletes it. Returns false when the IO is
     * not in any room.
     */
    static bool deleteIO(Calaos::IOBase *io);

    /* ---------------------------------------------------------------------
     * Rule helpers
     * ------------------------------------------------------------------ */

    /* Build "if <inputId> <oper> <inputValue> then <outputId> = <outputValue>"
     * directly with ConditionStd/ActionStd (no XML), add it to ListeRule and
     * return it. The two ids must already exist in ListeRoom.
     * Returns nullptr if one of them does not.
     */
    static Calaos::Rule *addSimpleRule(const std::string &name,
                                       const std::string &inputId,
                                       const std::string &oper,
                                       const std::string &inputValue,
                                       const std::string &outputId,
                                       const std::string &outputValue,
                                       const std::string &type = "rule");

    /* Same, but going through the XML path (RulesFactory), which is what
     * Config::LoadConfigRule() does. `ruleXml` is a single `<calaos:rule>`
     * element. Returns nullptr when the element cannot be parsed or when an id
     * it references is unknown.
     */
    static Calaos::Rule *addRuleFromXml(const std::string &ruleXml);

    /* First rule with that name, nullptr if none. */
    static Calaos::Rule *findRule(const std::string &name);

private:
    static void writeFile(const std::string &path, const std::string &content);
    static std::string readFile(const std::string &path);

    std::string baseDir;
    std::string confDir;
    std::string cachDir;
};

}

#endif
