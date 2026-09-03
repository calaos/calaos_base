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

//Implementation of the shared core test scaffolding, see CalaosCoreFixture.h
//for the documentation of every helper.

#include "CalaosCoreFixture.h"

#include "ActionStd.h"
#include "ConditionStd.h"
#include "RulesFactory.h"
#include "AutoScenarioDef.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include <sys/stat.h>

using namespace Calaos;

namespace CalaosTest
{

const char *const ID_BOOL_IN = "io_core_bool_in";
const char *const ID_BOOL_OUT = "io_core_bool_out";
const char *const ID_INT = "io_core_int";
const char *const ID_STRING = "io_core_string";
const char *const ROOM_NAME = "TestRoom";
const char *const ROOM_TYPE = "living";
const char *const RULE_NAME = "TestRule";

const char *const CoreFixture::ID_BOOL_IN = CalaosTest::ID_BOOL_IN;
const char *const CoreFixture::ID_BOOL_OUT = CalaosTest::ID_BOOL_OUT;
const char *const CoreFixture::ID_INT = CalaosTest::ID_INT;
const char *const CoreFixture::ID_STRING = CalaosTest::ID_STRING;
const char *const CoreFixture::ROOM_NAME = CalaosTest::ROOM_NAME;
const char *const CoreFixture::ROOM_TYPE = CalaosTest::ROOM_TYPE;
const char *const CoreFixture::RULE_NAME = CalaosTest::RULE_NAME;

/******************************************************************************
 * XML builders
 ******************************************************************************/

std::string internalIoXml(const std::string &type, const std::string &id,
                          const std::string &name,
                          const std::string &extraAttributes)
{
    std::string x = "    <calaos:internal type=\"" + type + "\"";
    x += " id=\"" + id + "\"";
    x += " name=\"" + name + "\"";
    x += " enabled=\"true\" visible=\"true\"";
    if (!extraAttributes.empty())
        x += " " + extraAttributes;
    x += " />\n";
    return x;
}

std::string roomXml(const std::string &name, const std::string &type,
                    const std::string &iosXml, int hits)
{
    std::ostringstream ss;
    ss << "  <calaos:room name=\"" << name << "\" type=\"" << type
       << "\" hits=\"" << hits << "\">\n"
       << iosXml
       << "  </calaos:room>\n";
    return ss.str();
}

std::string ioXmlDocument(const std::string &roomsXml)
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                       "<calaos:ioconfig xmlns:calaos=\"http://www.calaos.fr\">\n"
                       "<calaos:home>\n") +
           roomsXml +
           "</calaos:home>\n"
           "</calaos:ioconfig>\n";
}

std::string rulesXmlDocument(const std::string &rulesXml)
{
    return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                       "<calaos:rules xmlns:calaos=\"http://www.calaos.fr\">\n") +
           rulesXml +
           "</calaos:rules>\n";
}

std::string simpleRuleXml(const std::string &name,
                          const std::string &inputId,
                          const std::string &oper,
                          const std::string &inputValue,
                          const std::string &outputId,
                          const std::string &outputValue,
                          const std::string &type)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"" << name << "\" type=\"" << type << "\">\n"
       << "    <calaos:condition type=\"standard\" trigger=\"true\">\n"
       << "      <calaos:input id=\"" << inputId << "\" oper=\"" << oper
       << "\" val=\"" << inputValue << "\" />\n"
       << "    </calaos:condition>\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << outputId << "\" val=\"" << outputValue << "\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";
    return ss.str();
}

std::string minimalIoXml()
{
    std::string ios;
    ios += internalIoXml("InternalBool", ID_BOOL_IN, "Bool input");
    ios += internalIoXml("InternalBool", ID_BOOL_OUT, "Bool output");
    ios += internalIoXml("InternalInt", ID_INT, "Int value");
    ios += internalIoXml("InternalString", ID_STRING, "String value");

    return ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios, 0));
}

std::string minimalRulesXml()
{
    return rulesXmlDocument(simpleRuleXml(RULE_NAME, ID_BOOL_IN, "==", "true",
                                          ID_BOOL_OUT, "true"));
}

/******************************************************************************
 * Fixture
 ******************************************************************************/

void CoreFixture::SetUp()
{
    //A private tree for this test only: <tmp>/calaos_coretest_XXXXXX/{config,cache}
    std::string tmpDir = "/tmp";
    if (const char *t = ::getenv("TMPDIR"))
    {
        if (*t)
            tmpDir = t;
    }

    std::string tmpl = tmpDir + "/calaos_coretest_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');

    char *d = ::mkdtemp(buf.data());
    ASSERT_NE(d, nullptr) << "could not create a temporary directory in " << tmpDir;
    baseDir = d;

    confDir = baseDir + "/config";
    cachDir = baseDir + "/cache";
    ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);
    ASSERT_EQ(::mkdir(cachDir.c_str(), 0755), 0);

    //initConfigOptions() is the only entry point resetting the config/cache
    //paths cached by Utils. It also sets CALAOS_CONFIG and seeds a default
    //local_config.xml, which several core code paths read.
    std::vector<char> c(confDir.begin(), confDir.end());
    c.push_back('\0');
    std::vector<char> k(cachDir.begin(), cachDir.end());
    k.push_back('\0');
    Utils::initConfigOptions(c.data(), k.data(), true);

    //A previous test of the same binary may have left rooms/rules behind
    clearCoreState();
}

void CoreFixture::TearDown()
{
    clearCoreState();

    if (!baseDir.empty())
    {
        std::string cmd = "rm -rf '" + baseDir + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << baseDir << std::endl;
    }
}

void CoreFixture::writeFile(const std::string &path, const std::string &content)
{
    std::ofstream f(path.c_str(), std::ofstream::out | std::ofstream::trunc);
    f << content;
    f.close();
}

std::string CoreFixture::readFile(const std::string &path)
{
    std::ifstream f(path.c_str());
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void CoreFixture::writeConfig(const std::string &ioXml, const std::string &rulesXml)
{
    writeFile(confDir + "/" IO_CONFIG, ioXml);
    writeFile(confDir + "/" RULES_CONFIG, rulesXml);
}

void CoreFixture::loadConfig(const std::string &ioXml, const std::string &rulesXml)
{
    writeConfig(ioXml, rulesXml);

    //Same order as calaos_server's main(): IOs first, the rules resolve their
    //ids against ListeRoom while they load.
    Config::Instance().LoadConfigIO();
    Config::Instance().LoadConfigRule();
}

void CoreFixture::saveConfig()
{
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();
}

void CoreFixture::reloadFromDisk()
{
    clearCoreState();
    Config::Instance().LoadConfigIO();
    Config::Instance().LoadConfigRule();
}

std::string CoreFixture::ioXmlOnDisk() const
{
    return readFile(confDir + "/" IO_CONFIG);
}

std::string CoreFixture::rulesXmlOnDisk() const
{
    return readFile(confDir + "/" RULES_CONFIG);
}

void CoreFixture::clearCoreState()
{
    //Rules first: deleting an IO walks the rule list to drop the rules using it
    ListeRule &rules = ListeRule::Instance();
    while (rules.size() > 0)
        rules.Remove(0);

    //~Room() deletes the IOs of the room, and ~IOBase() unregisters them from
    //the id hash table of ListeRoom
    ListeRoom &rooms = ListeRoom::Instance();
    while (rooms.size() > 0)
        rooms.Remove(0);

    /* The auto scenario id allocators are process wide and this harness runs
     * dozens of unrelated configurations in one process: without this, the
     * step ids a scenario is given depend on how many cases ran before, and
     * every golden carrying one becomes order dependent.
     */
    AutoScenarioDef::resetIdAllocatorsForTests();
}

void CoreFixture::forgetIOState(const std::string &id)
{
    //There is no way to erase an entry of the state cache, overwriting it with
    //an empty value is the closest thing the public API offers. The `false`
    //keeps the cache file untouched.
    Config::Instance().SaveValueIO(id, std::string(), false);
    Config::Instance().SaveValueParams(id, Params(), false);
}

/******************************************************************************
 * Dumps and round trip
 ******************************************************************************/

std::string CoreFixture::dumpIos()
{
    std::ostringstream ss;

    ListeRoom &rooms = ListeRoom::Instance();
    for (int i = 0;i < rooms.size();i++)
    {
        Room *room = rooms.get_room(i);
        ss << "room[" << i << "] name=" << room->get_name()
           << " type=" << room->get_type()
           << " hits=" << room->get_hits()
           << " ios=" << room->get_size() << "\n";

        for (int j = 0;j < room->get_size();j++)
        {
            IOBase *io = room->get_io(j);
            ss << "  io[" << j << "]";

            //Params is a std::map, the iteration order is stable
            Params &p = io->get_params();
            for (int k = 0;k < p.size();k++)
            {
                std::string key, value;
                p.get_item(k, key, value);
                ss << " " << key << "=" << value;
            }
            ss << "\n";
        }
    }

    return ss.str();
}

std::string CoreFixture::dumpRules()
{
    std::ostringstream ss;

    //Serializing through the real SaveToXml() covers the conditions and the
    //actions of every rule type without duplicating their internals here.
    pugi::xml_document doc;
    pugi::xml_node root = doc.append_child("calaos:rules");
    ListeRule &rules = ListeRule::Instance();
    for (int i = 0;i < rules.size();i++)
        rules.get_rule(i)->SaveToXml(root);

    root.print(ss, "  ");
    return ss.str();
}

::testing::AssertionResult CoreFixture::roundTripIo()
{
    std::string before = dumpIos();

    saveConfig();
    std::string fileBefore = ioXmlOnDisk();

    reloadFromDisk();
    std::string after = dumpIos();

    if (before != after)
    {
        return ::testing::AssertionFailure()
               << "the IOs changed across a save/load cycle\n"
               << "--- before ---\n" << before
               << "--- after ----\n" << after;
    }

    saveConfig();
    std::string fileAfter = ioXmlOnDisk();

    if (fileBefore != fileAfter)
    {
        return ::testing::AssertionFailure()
               << "io.xml is not stable across a save/load/save cycle\n"
               << "--- first save ---\n" << fileBefore
               << "--- second save --\n" << fileAfter;
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult CoreFixture::roundTripRules()
{
    std::string before = dumpRules();

    saveConfig();
    std::string fileBefore = rulesXmlOnDisk();

    reloadFromDisk();
    std::string after = dumpRules();

    if (before != after)
    {
        return ::testing::AssertionFailure()
               << "the rules changed across a save/load cycle\n"
               << "--- before ---\n" << before
               << "--- after ----\n" << after;
    }

    saveConfig();
    std::string fileAfter = rulesXmlOnDisk();

    if (fileBefore != fileAfter)
    {
        return ::testing::AssertionFailure()
               << "rules.xml is not stable across a save/load/save cycle\n"
               << "--- first save ---\n" << fileBefore
               << "--- second save --\n" << fileAfter;
    }

    return ::testing::AssertionSuccess();
}

/******************************************************************************
 * IO helpers
 ******************************************************************************/

IOBase *CoreFixture::io(const std::string &id)
{
    return ListeRoom::Instance().get_io(id);
}

Room *CoreFixture::firstRoom()
{
    if (ListeRoom::Instance().size() <= 0)
        return nullptr;
    return ListeRoom::Instance().get_room(0);
}

Room *CoreFixture::findRoom(const std::string &name)
{
    ListeRoom &rooms = ListeRoom::Instance();
    for (int i = 0;i < rooms.size();i++)
    {
        if (rooms.get_room(i)->get_name() == name)
            return rooms.get_room(i);
    }
    return nullptr;
}

Room *CoreFixture::addRoom(const std::string &name, const std::string &type)
{
    Room *room = new Room(name, type, 0);
    ListeRoom::Instance().Add(room);
    return room;
}

IOBase *CoreFixture::createIO(Params params, Room *room)
{
    if (!room)
        room = firstRoom();
    if (!room)
        return nullptr;

    return ListeRoom::Instance().createIO(params, room);
}

IOBase *CoreFixture::createInternalIO(const std::string &type,
                                      const std::string &id,
                                      const std::string &name,
                                      Room *room)
{
    Params p = { { "type", type },
                 { "id", id },
                 { "name", name },
                 { "enabled", "true" },
                 { "visible", "true" } };

    return createIO(p, room);
}

bool CoreFixture::deleteIO(IOBase *io)
{
    if (!io)
        return false;

    //Same call the JSON API uses: drops the rules using this IO, then removes
    //it from its room and deletes it
    return ListeRoom::Instance().deleteIO(io);
}

/******************************************************************************
 * Rule helpers
 ******************************************************************************/

Rule *CoreFixture::addSimpleRule(const std::string &name,
                                 const std::string &inputId,
                                 const std::string &oper,
                                 const std::string &inputValue,
                                 const std::string &outputId,
                                 const std::string &outputValue,
                                 const std::string &type)
{
    IOBase *input = io(inputId);
    IOBase *output = io(outputId);

    if (!input || !output)
        return nullptr;

    Rule *rule = new Rule(type, name);

    ConditionStd *cond = new ConditionStd();
    cond->Add(input);
    cond->get_params().Add(inputId, inputValue);
    cond->get_operator().Add(inputId, oper);
    rule->AddCondition(cond);

    ActionStd *action = new ActionStd();
    action->Add(output);
    action->get_params().Add(outputId, outputValue);
    rule->AddAction(action);

    ListeRule::Instance().Add(rule);

    return rule;
}

Rule *CoreFixture::addRuleFromXml(const std::string &ruleXml)
{
    pugi::xml_document document;
    if (!document.load_string(ruleXml.c_str()))
        return nullptr;

    pugi::xml_node node = document.document_element();
    if (!node || std::string(node.name()) != "calaos:rule" ||
        !node.attribute("name") || !node.attribute("type"))
        return nullptr;

    //Same sequence as Config::LoadConfigRule()
    Rule *rule = new Rule(node.attribute("type").as_string(), node.attribute("name").as_string());
    rule->LoadFromXml(node);
    ListeRule::Instance().Add(rule);

    return rule;
}

Rule *CoreFixture::findRule(const std::string &name)
{
    ListeRule &rules = ListeRule::Instance();
    for (int i = 0;i < rules.size();i++)
    {
        if (rules.get_rule(i)->get_name() == name)
            return rules.get_rule(i);
    }
    return nullptr;
}

}
