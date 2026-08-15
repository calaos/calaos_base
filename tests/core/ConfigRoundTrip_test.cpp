/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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

/******************************************************************************
 * E4.3b — the config round trip referee.
 *
 * CalaosCoreFixture could already save a config, but no test ever asserted
 * that loading it back gives the same thing. This suite does, and it is meant
 * to stay the referee of the tinyxml -> pugixml migration (E4.4d), which will
 * legitimately reformat io.xml and rules.xml.
 *
 * Therefore: everything below compares the *semantic* model held in memory
 * (rooms, IO ids, IO parameters, time ranges, rules with their inputs,
 * operators and values), never the bytes of the files. Indentation, attribute
 * order, quoting style, the XML declaration, the way an entity is spelled -
 * none of it is asserted anywhere. What is asserted:
 *
 *   config -> save -> reload -> the model is identical, and the values are
 *   still the exact strings that were put in.
 *
 * The payloads deliberately contain the things a serializer gets wrong:
 * accented UTF-8, the five XML specials (& < > " '), numeric character
 * references, empty attribute values and an InPlageHoraire, whose schedule
 * lives in child elements rather than in attributes.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"

#include "ActionStd.h"
#include "ConditionStd.h"
#include "InPlageHoraire.h"

using namespace Calaos;

namespace
{

/******************************************************************************
 * A semantic snapshot of everything Config persists.
 ******************************************************************************/

struct IoSnapshot
{
    std::string room;                            //name of the owning room
    int indexInRoom = -1;                        //position inside that room
    std::map<std::string, std::string> params;   //every parameter of the IO
};

struct RoomSnapshot
{
    std::string name;
    std::string type;
    int hits = 0;
    std::vector<std::string> ioIds;
};

struct RuleSnapshot
{
    std::string name;
    std::string type;
    std::vector<std::string> conditions;  //canonical, sorted
    std::vector<std::string> actions;     //canonical, sorted
};

struct ConfigSnapshot
{
    std::vector<RoomSnapshot> rooms;
    std::map<std::string, IoSnapshot> ios;   //keyed by IO id
    std::vector<RuleSnapshot> rules;
};

std::string join(std::vector<std::string> items)
{
    std::sort(items.begin(), items.end());

    std::string s;
    for (const std::string &i: items)
    {
        if (!s.empty()) s += " | ";
        s += i;
    }
    return s;
}

std::string describeCondition(Condition *c)
{
    ConditionStd *std_ = dynamic_cast<ConditionStd *>(c);
    if (!std_)
        return "condition_type=" + Utils::to_string(c->getType());

    std::vector<std::string> items;
    for (int i = 0; i < std_->get_size(); i++)
    {
        IOBase *in = std_->get_input(i);
        const std::string id = in ? in->get_param("id") : std::string("<null>");
        items.push_back(id + " " + std_->get_operator()[id] +
                        " '" + std_->get_params()[id] + "'");
    }

    return std::string("std(trigger=") + (std_->useForTrigger() ? "true" : "false") +
           ")[" + join(items) + "]";
}

std::string describeAction(Action *a)
{
    ActionStd *std_ = dynamic_cast<ActionStd *>(a);
    if (!std_)
        return "action_type=" + Utils::to_string(a->getType());

    std::vector<std::string> items;
    for (int i = 0; i < std_->get_size(); i++)
    {
        IOBase *out = std_->get_output(i);
        const std::string id = out ? out->get_param("id") : std::string("<null>");
        items.push_back(id + " = '" + std_->get_params()[id] + "'");
    }

    return "std[" + join(items) + "]";
}

ConfigSnapshot takeSnapshot()
{
    ConfigSnapshot snap;

    ListeRoom &rooms = ListeRoom::Instance();
    for (int i = 0; i < rooms.size(); i++)
    {
        Room *room = rooms.get_room(i);

        RoomSnapshot r;
        r.name = room->get_name();
        r.type = room->get_type();
        r.hits = room->get_hits();

        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            const std::string id = io->get_param("id");
            r.ioIds.push_back(id);

            IoSnapshot s;
            s.room = r.name;
            s.indexInRoom = j;

            Params &p = io->get_params();
            for (int k = 0; k < p.size(); k++)
            {
                std::string key, value;
                p.get_item(k, key, value);
                s.params[key] = value;
            }

            snap.ios[id] = s;
        }

        snap.rooms.push_back(r);
    }

    ListeRule &rules = ListeRule::Instance();
    for (int i = 0; i < rules.size(); i++)
    {
        Rule *rule = rules.get_rule(i);

        RuleSnapshot r;
        r.name = rule->get_name();
        r.type = rule->get_type();

        for (int j = 0; j < rule->get_size_conds(); j++)
            r.conditions.push_back(describeCondition(rule->get_condition(j)));
        for (int j = 0; j < rule->get_size_actions(); j++)
            r.actions.push_back(describeAction(rule->get_action(j)));

        std::sort(r.conditions.begin(), r.conditions.end());
        std::sort(r.actions.begin(), r.actions.end());

        snap.rules.push_back(r);
    }

    return snap;
}

//Granular comparison: a failure names the room, the IO id and the parameter
//that moved, instead of dumping two big blobs.
void expectSameConfig(const ConfigSnapshot &before, const ConfigSnapshot &after)
{
    ASSERT_EQ(before.rooms.size(), after.rooms.size()) << "the number of rooms changed";
    for (size_t i = 0; i < before.rooms.size(); i++)
    {
        const RoomSnapshot &b = before.rooms[i];
        const RoomSnapshot &a = after.rooms[i];

        EXPECT_EQ(b.name, a.name) << "room " << i << ": name";
        EXPECT_EQ(b.type, a.type) << "room " << b.name << ": type";
        EXPECT_EQ(b.hits, a.hits) << "room " << b.name << ": hits";
        EXPECT_EQ(b.ioIds, a.ioIds) << "room " << b.name << ": IO list/order";
    }

    std::set<std::string> beforeIds, afterIds;
    for (const auto &it: before.ios) beforeIds.insert(it.first);
    for (const auto &it: after.ios) afterIds.insert(it.first);
    ASSERT_EQ(beforeIds, afterIds) << "the set of IO ids changed";

    for (const auto &it: before.ios)
    {
        const std::string &id = it.first;
        const IoSnapshot &b = it.second;
        const IoSnapshot &a = after.ios.at(id);

        EXPECT_EQ(b.room, a.room) << "io " << id << ": owning room";
        EXPECT_EQ(b.indexInRoom, a.indexInRoom) << "io " << id << ": position in room";

        //compare key by key so a lost/added/rewritten parameter is named
        for (const auto &param: b.params)
        {
            auto found = a.params.find(param.first);
            if (found == a.params.end())
            {
                ADD_FAILURE() << "io " << id << ": parameter '" << param.first
                              << "' was lost (value was '" << param.second << "')";
                continue;
            }
            EXPECT_EQ(param.second, found->second)
                << "io " << id << ": parameter '" << param.first << "'";
        }
        for (const auto &param: a.params)
        {
            if (b.params.find(param.first) == b.params.end())
                ADD_FAILURE() << "io " << id << ": parameter '" << param.first
                              << "' appeared out of nowhere (value '"
                              << param.second << "')";
        }
    }

    ASSERT_EQ(before.rules.size(), after.rules.size()) << "the number of rules changed";
    for (size_t i = 0; i < before.rules.size(); i++)
    {
        const RuleSnapshot &b = before.rules[i];
        const RuleSnapshot &a = after.rules[i];

        EXPECT_EQ(b.name, a.name) << "rule " << i << ": name";
        EXPECT_EQ(b.type, a.type) << "rule " << b.name << ": type";
        EXPECT_EQ(b.conditions, a.conditions) << "rule " << b.name << ": conditions";
        EXPECT_EQ(b.actions, a.actions) << "rule " << b.name << ": actions";
    }
}

//Flatten the schedule of an InPlageHoraire the same way for both sides of a
//round trip: 7 vectors of ranges + the month mask, expressed as plain strings.
std::string describeSchedule(InPlageHoraire *io)
{
    std::string s = "months=" + io->months.to_string() + "\n";

    const char *names[7] = { "sunday", "monday", "tuesday", "wednesday",
                             "thursday", "friday", "saturday" };
    std::vector<TimeRange> *days[7] = { &io->getSunday(), &io->getMonday(),
                                        &io->getTuesday(), &io->getWednesday(),
                                        &io->getThursday(), &io->getFriday(),
                                        &io->getSaturday() };

    for (int d = 0; d < 7; d++)
    {
        s += names[d];
        s += ":";
        for (size_t i = 0; i < days[d]->size(); i++)
            s += " " + (*days[d])[i].toProtoCommand(d);
        s += "\n";
    }

    return s;
}

}

/******************************************************************************
 * The fixture
 ******************************************************************************/

class ConfigRoundTripTest: public CalaosTest::CoreFixture
{
protected:
    //config -> save -> reload -> same model. Returns the snapshot taken after
    //the reload so a test can assert on the values themselves too.
    ConfigSnapshot roundTrip()
    {
        const ConfigSnapshot before = takeSnapshot();

        saveConfig();
        reloadFromDisk();

        const ConfigSnapshot after = takeSnapshot();
        expectSameConfig(before, after);

        return after;
    }
};

/******************************************************************************
 * The minimal config
 ******************************************************************************/

TEST_F(ConfigRoundTripTest, MinimalConfigSurvivesSaveAndReload)
{
    loadConfig();

    const ConfigSnapshot after = roundTrip();

    //not just "identical to itself": the content is really there
    ASSERT_EQ(1u, after.rooms.size());
    EXPECT_EQ(ROOM_NAME, after.rooms[0].name);
    EXPECT_EQ(ROOM_TYPE, after.rooms[0].type);
    EXPECT_EQ(4u, after.rooms[0].ioIds.size());

    EXPECT_EQ(1u, after.ios.count(ID_BOOL_IN));
    EXPECT_EQ(1u, after.ios.count(ID_BOOL_OUT));
    EXPECT_EQ(1u, after.ios.count(ID_INT));
    EXPECT_EQ(1u, after.ios.count(ID_STRING));
    EXPECT_EQ("InternalBool", after.ios.at(ID_BOOL_IN).params.at("type"));

    ASSERT_EQ(1u, after.rules.size());
    EXPECT_EQ(RULE_NAME, after.rules[0].name);

    //and the objects really are usable again after the reload
    ASSERT_NE(nullptr, io(ID_BOOL_IN));
    ASSERT_NE(nullptr, findRule(RULE_NAME));
}

TEST_F(ConfigRoundTripTest, SecondSaveIsIdenticalToTheFirst)
{
    //Not a formatting assertion about how E4.4d writes XML: both files here
    //are produced by the *same* writer, so this only says that a save/load
    //cycle is a fixed point. It is the cheapest guard against a value that
    //mutates a little on every reload.
    loadConfig();

    EXPECT_TRUE(roundTripIo());
    EXPECT_TRUE(roundTripRules());
}

TEST_F(ConfigRoundTripTest, ReloadIsIdempotent)
{
    loadConfig();

    saveConfig();
    reloadFromDisk();
    const ConfigSnapshot first = takeSnapshot();

    saveConfig();
    reloadFromDisk();
    const ConfigSnapshot second = takeSnapshot();

    expectSameConfig(first, second);
}

/******************************************************************************
 * Accents, XML entities, numeric character references
 ******************************************************************************/

TEST_F(ConfigRoundTripTest, AccentedAndEscapedValuesSurvive)
{
    //Raw UTF-8, the five XML specials, and a numeric character reference. The
    //reference decodes to the same character as the raw one, so the *value* is
    //compared, never the spelling.
    const std::string accented = "Lumi\xC3\xA8re du s\xC3\xA9jour";        //Lumière du séjour
    const std::string escaped = "A & B < C > D \" E ' F";
    const std::string numeric = "caf\xC3\xA9";                             //café

    std::string ios;
    ios += CalaosTest::internalIoXml("InternalBool", "rt_accents",
                                     "Lumi&#232;re du s&#233;jour");
    ios += CalaosTest::internalIoXml("InternalString", "rt_escaped",
                                     "A &amp; B &lt; C &gt; D &quot; E ' F");
    ios += CalaosTest::internalIoXml("InternalInt", "rt_numeric", "caf&#233;");

    loadConfig(CalaosTest::ioXmlDocument(
                   CalaosTest::roomXml("Salon \xC3\xA9""clair\xC3\xA9", "salon", ios, 7)),
               CalaosTest::rulesXmlDocument(std::string()));

    //what was loaded is what was meant, before any save
    ASSERT_NE(nullptr, io("rt_accents"));
    EXPECT_EQ(accented, io("rt_accents")->get_param("name"));
    EXPECT_EQ(escaped, io("rt_escaped")->get_param("name"));
    EXPECT_EQ(numeric, io("rt_numeric")->get_param("name"));

    const ConfigSnapshot after = roundTrip();

    //...and it is still what it was after save + reload
    ASSERT_EQ(1u, after.rooms.size());
    EXPECT_EQ("Salon \xC3\xA9""clair\xC3\xA9", after.rooms[0].name);
    EXPECT_EQ(7, after.rooms[0].hits);

    EXPECT_EQ(accented, after.ios.at("rt_accents").params.at("name"));
    EXPECT_EQ(escaped, after.ios.at("rt_escaped").params.at("name"));
    EXPECT_EQ(numeric, after.ios.at("rt_numeric").params.at("name"));
}

TEST_F(ConfigRoundTripTest, EmptyValuesSurvive)
{
    //An empty attribute must come back as an empty string, not as a missing
    //parameter and not as some placeholder.
    std::string ios;
    ios += CalaosTest::internalIoXml("InternalBool", "rt_empty_name", "");
    //`unit` is a free parameter nothing rewrites. (A forced one such as
    //gui_type would be reset by the IO constructor, which has nothing to do
    //with the round trip.)
    ios += CalaosTest::internalIoXml("InternalString", "rt_empty_param", "Named",
                                     "unit=\"\" description=\"\"");

    loadConfig(CalaosTest::ioXmlDocument(
                   CalaosTest::roomXml("Cave", "cave", ios, 0)),
               CalaosTest::rulesXmlDocument(std::string()));

    ASSERT_NE(nullptr, io("rt_empty_name"));
    ASSERT_TRUE(io("rt_empty_param")->get_params().Exists("unit"));
    EXPECT_EQ("", io("rt_empty_param")->get_param("unit"));

    const ConfigSnapshot after = roundTrip();

    ASSERT_EQ(1u, after.ios.count("rt_empty_name"));
    EXPECT_EQ("", after.ios.at("rt_empty_name").params.at("name"));

    const auto &params = after.ios.at("rt_empty_param").params;
    ASSERT_EQ(1u, params.count("unit")) << "an empty parameter must not disappear";
    EXPECT_EQ("", params.at("unit"));
    ASSERT_EQ(1u, params.count("description"));
    EXPECT_EQ("", params.at("description"));
}

TEST_F(ConfigRoundTripTest, EmptyRoomSurvives)
{
    //A room with no IO at all must still be there after the reload
    std::string rooms;
    rooms += CalaosTest::roomXml("Grenier", "grenier", std::string(), 0);
    rooms += CalaosTest::roomXml("Garage", "garage",
                                 CalaosTest::internalIoXml("InternalBool",
                                                           "rt_garage", "Porte"),
                                 2);

    loadConfig(CalaosTest::ioXmlDocument(rooms),
               CalaosTest::rulesXmlDocument(std::string()));

    const ConfigSnapshot after = roundTrip();

    ASSERT_EQ(2u, after.rooms.size());
    EXPECT_EQ("Grenier", after.rooms[0].name);
    EXPECT_TRUE(after.rooms[0].ioIds.empty());
    EXPECT_EQ("Garage", after.rooms[1].name);
    EXPECT_EQ(1u, after.rooms[1].ioIds.size());
}

/******************************************************************************
 * Several rooms, order and ids
 ******************************************************************************/

TEST_F(ConfigRoundTripTest, RoomAndIoOrderIsPreserved)
{
    std::string rooms;
    for (int r = 0; r < 3; r++)
    {
        std::string ios;
        for (int i = 0; i < 3; i++)
        {
            ios += CalaosTest::internalIoXml("InternalBool",
                                             "rt_order_" + Utils::to_string(r) +
                                             "_" + Utils::to_string(i),
                                             "IO " + Utils::to_string(i));
        }
        rooms += CalaosTest::roomXml("Room" + Utils::to_string(r),
                                     "type" + Utils::to_string(r), ios, r);
    }

    loadConfig(CalaosTest::ioXmlDocument(rooms),
               CalaosTest::rulesXmlDocument(std::string()));

    const ConfigSnapshot after = roundTrip();

    ASSERT_EQ(3u, after.rooms.size());
    for (int r = 0; r < 3; r++)
    {
        EXPECT_EQ("Room" + Utils::to_string(r), after.rooms[r].name);
        EXPECT_EQ(r, after.rooms[r].hits);
        ASSERT_EQ(3u, after.rooms[r].ioIds.size());
        for (int i = 0; i < 3; i++)
        {
            EXPECT_EQ("rt_order_" + Utils::to_string(r) + "_" + Utils::to_string(i),
                      after.rooms[r].ioIds[i]);
        }
    }
}

/******************************************************************************
 * Rules
 ******************************************************************************/

TEST_F(ConfigRoundTripTest, RulesSurviveWithTheirInputsOperatorsAndValues)
{
    std::string ios;
    ios += CalaosTest::internalIoXml("InternalBool", "rt_rule_in", "Entr&#233;e");
    ios += CalaosTest::internalIoXml("InternalBool", "rt_rule_out", "Sortie");
    ios += CalaosTest::internalIoXml("InternalInt", "rt_rule_int", "Consigne");
    ios += CalaosTest::internalIoXml("InternalString", "rt_rule_str", "Message");

    std::string rules;
    rules += CalaosTest::simpleRuleXml("R\xC3\xA8gle nuit &amp; jour", "rt_rule_in",
                                       "==", "true", "rt_rule_out", "true");
    rules += CalaosTest::simpleRuleXml("Consigne", "rt_rule_int",
                                       ">", "21", "rt_rule_str",
                                       "trop chaud &lt;&gt;", "scenario");

    loadConfig(CalaosTest::ioXmlDocument(
                   CalaosTest::roomXml("Chambre", "chambre", ios, 1)),
               CalaosTest::rulesXmlDocument(rules));

    ASSERT_EQ(2, ListeRule::Instance().size());

    const ConfigSnapshot after = roundTrip();

    ASSERT_EQ(2u, after.rules.size());
    EXPECT_EQ("R\xC3\xA8gle nuit & jour", after.rules[0].name);
    EXPECT_EQ("rule", after.rules[0].type);
    ASSERT_EQ(1u, after.rules[0].conditions.size());
    EXPECT_EQ("std(trigger=true)[rt_rule_in == 'true']", after.rules[0].conditions[0]);
    ASSERT_EQ(1u, after.rules[0].actions.size());
    EXPECT_EQ("std[rt_rule_out = 'true']", after.rules[0].actions[0]);

    EXPECT_EQ("Consigne", after.rules[1].name);
    EXPECT_EQ("scenario", after.rules[1].type);
    EXPECT_EQ("std(trigger=true)[rt_rule_int > '21']", after.rules[1].conditions[0]);
    EXPECT_EQ("std[rt_rule_str = 'trop chaud <>']", after.rules[1].actions[0]);
}

/******************************************************************************
 * InPlageHoraire: the only core IO whose config lives in child elements
 ******************************************************************************/

TEST_F(ConfigRoundTripTest, TimeRangeIoScheduleSurvives)
{
    //Two ranges on monday (one normal, one sunset based with an offset), one
    //on sunday, nothing on the other days, and a month mask with holes.
    const std::string plageXml =
        "    <calaos:input type=\"InPlageHoraire\" id=\"rt_plage\""
        " name=\"Plage horaire\" enabled=\"true\" months=\"101100110011\">\n"
        "      <calaos:lundi>\n"
        "        <calaos:plage start_type=\"0\" start_hour=\"8\" start_min=\"30\""
        " start_sec=\"0\" end_type=\"0\" end_hour=\"12\" end_min=\"0\" end_sec=\"0\" />\n"
        "        <calaos:plage start_type=\"2\" start_hour=\"0\" start_min=\"15\""
        " start_sec=\"0\" start_offset=\"-1\""
        " end_type=\"0\" end_hour=\"23\" end_min=\"59\" end_sec=\"59\" />\n"
        "      </calaos:lundi>\n"
        "      <calaos:dimanche>\n"
        "        <calaos:plage start_type=\"0\" start_hour=\"0\" start_min=\"0\""
        " start_sec=\"0\" end_type=\"0\" end_hour=\"23\" end_min=\"59\" end_sec=\"59\" />\n"
        "      </calaos:dimanche>\n"
        "    </calaos:input>\n";

    loadConfig(CalaosTest::ioXmlDocument(
                   CalaosTest::roomXml("Horaires", "technique", plageXml, 0)),
               CalaosTest::rulesXmlDocument(std::string()));

    InPlageHoraire *before = dynamic_cast<InPlageHoraire *>(io("rt_plage"));
    ASSERT_NE(nullptr, before) << "InPlageHoraire is not registered in this binary";

    //the schedule really was read (and not silently dropped)
    ASSERT_EQ(2u, before->getMonday().size());
    ASSERT_EQ(1u, before->getSunday().size());
    EXPECT_TRUE(before->getTuesday().empty());
    EXPECT_EQ(8 * 3600 + 30 * 60, before->getMonday()[0].getStartTimeSec(2025, 6, 16));
    EXPECT_EQ(TimeRange::HTYPE_SUNSET, before->getMonday()[1].start_type);
    EXPECT_EQ(-1, before->getMonday()[1].start_offset);
    EXPECT_EQ(7u, before->months.count()); //101100110011

    //Everything needed after the reload has to be copied now: reloadFromDisk()
    //destroys every IO, `before` is dangling once roundTrip() returns.
    const std::string scheduleBefore = describeSchedule(before);
    const std::string monthsBefore = before->months.to_string();
    const std::vector<TimeRange> mondayBefore = before->getMonday();
    before = nullptr;

    roundTrip();

    InPlageHoraire *after = dynamic_cast<InPlageHoraire *>(io("rt_plage"));
    ASSERT_NE(nullptr, after);
    EXPECT_EQ(scheduleBefore, describeSchedule(after));

    //spelled out, so a failure says which bound moved
    ASSERT_EQ(2u, after->getMonday().size());
    EXPECT_TRUE(mondayBefore[0] == after->getMonday()[0]);
    EXPECT_EQ(8 * 3600 + 30 * 60, after->getMonday()[0].getStartTimeSec(2025, 6, 16));
    EXPECT_EQ(12 * 3600, after->getMonday()[0].getEndTimeSec(2025, 6, 16));
    EXPECT_EQ(TimeRange::HTYPE_SUNSET, after->getMonday()[1].start_type);
    EXPECT_EQ("15", after->getMonday()[1].smin);
    EXPECT_EQ(-1, after->getMonday()[1].start_offset);
    ASSERT_EQ(1u, after->getSunday().size());
    EXPECT_TRUE(after->getTuesday().empty());
    EXPECT_EQ(7u, after->months.count());
    EXPECT_EQ(monthsBefore, after->months.to_string());
}

TEST_F(ConfigRoundTripTest, TimeRangeIoWithoutAnyRangeSurvives)
{
    //SaveRange() skips the empty days entirely: nothing must come back
    const std::string plageXml =
        "    <calaos:input type=\"InPlageHoraire\" id=\"rt_plage_empty\""
        " name=\"Vide\" enabled=\"true\" months=\"111111111111\" />\n";

    loadConfig(CalaosTest::ioXmlDocument(
                   CalaosTest::roomXml("Horaires", "technique", plageXml, 0)),
               CalaosTest::rulesXmlDocument(std::string()));

    InPlageHoraire *before = dynamic_cast<InPlageHoraire *>(io("rt_plage_empty"));
    ASSERT_NE(nullptr, before);
    EXPECT_EQ(12u, before->months.count());

    roundTrip();

    InPlageHoraire *after = dynamic_cast<InPlageHoraire *>(io("rt_plage_empty"));
    ASSERT_NE(nullptr, after);
    EXPECT_TRUE(after->getMonday().empty());
    EXPECT_TRUE(after->getSunday().empty());
    EXPECT_EQ(12u, after->months.count());
}
