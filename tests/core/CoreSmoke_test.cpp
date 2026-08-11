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

//Smoke tests of the calaos_server core: a minimal io.xml/rules.xml is loaded
//through the real Config/IOFactory/RulesFactory code path and the resulting
//object graph is checked. They also exercise every helper of CoreFixture, so a
//breakage of the scaffolding itself is caught here and not in T1.x.

#include "CalaosCoreFixture.h"

using namespace Calaos;
using namespace CalaosTest;

class CoreSmokeTest: public CoreFixture
{
};

//The fixture must hand over an empty world to every test, whatever the
//previous one left behind (all of this is singleton state).
TEST_F(CoreSmokeTest, StartsFromACleanState)
{
    EXPECT_EQ(ListeRoom::Instance().size(), 0);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 0);
    EXPECT_EQ(ListeRule::Instance().size(), 0);
}

//The smoke test: load a minimal config, the IOs are instantiated and findable
//by id in ListeRoom.
TEST_F(CoreSmokeTest, LoadsMinimalConfigAndFindsIosById)
{
    loadConfig();

    ASSERT_EQ(ListeRoom::Instance().size(), 1);

    Room *room = firstRoom();
    ASSERT_NE(room, nullptr);
    EXPECT_EQ(room->get_name(), ROOM_NAME);
    EXPECT_EQ(room->get_type(), ROOM_TYPE);
    EXPECT_EQ(room->get_size(), 4);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 4);

    IOBase *boolIn = io(ID_BOOL_IN);
    ASSERT_NE(boolIn, nullptr) << "IO " << ID_BOOL_IN << " was not instantiated";
    EXPECT_EQ(boolIn->get_param("id"), ID_BOOL_IN);
    EXPECT_EQ(boolIn->get_param("name"), "Bool input");
    EXPECT_EQ(boolIn->get_param("type"), "InternalBool");
    EXPECT_EQ(boolIn->get_type(), TBOOL);
    EXPECT_TRUE(boolIn->isEnabled());
    //Internal IOs are inout, the rules engine may read them and write them
    EXPECT_TRUE(boolIn->isInput());
    EXPECT_TRUE(boolIn->isOutput());
    //gui_type is derived from the type by the Internal constructor
    EXPECT_EQ(boolIn->get_param("gui_type"), "var_bool");

    ASSERT_NE(io(ID_INT), nullptr);
    EXPECT_EQ(io(ID_INT)->get_type(), TINT);
    ASSERT_NE(io(ID_STRING), nullptr);
    EXPECT_EQ(io(ID_STRING)->get_type(), TSTRING);

    //The room the IO belongs to can be walked back
    EXPECT_EQ(ListeRoom::Instance().getRoomByIO(boolIn), room);

    //And an unknown id is a null pointer, not a crash
    EXPECT_EQ(io("this-id-does-not-exist"), nullptr);
}

//An unknown type is skipped silently by IOFactory (documented behaviour), the
//rest of the file must still load.
TEST_F(CoreSmokeTest, UnknownIoTypeIsSkipped)
{
    std::string ios;
    ios += internalIoXml("InternalBool", ID_BOOL_IN, "Bool input");
    ios += internalIoXml("ThisTypeDoesNotExist", "io_unknown", "Unknown");

    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios)),
               rulesXmlDocument(""));

    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 1);
    EXPECT_NE(io(ID_BOOL_IN), nullptr);
    EXPECT_EQ(io("io_unknown"), nullptr);
}

//Rules are loaded with their conditions and their actions, and their ids are
//resolved against the IOs loaded just before.
TEST_F(CoreSmokeTest, LoadsMinimalRules)
{
    loadConfig();

    ASSERT_EQ(ListeRule::Instance().size(), 1);

    Rule *rule = findRule(RULE_NAME);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->get_type(), "rule");
    ASSERT_EQ(rule->get_size_conds(), 1);
    ASSERT_EQ(rule->get_size_actions(), 1);

    ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_EQ(cond->get_size(), 1);
    EXPECT_EQ(cond->get_input(0), io(ID_BOOL_IN));
    EXPECT_EQ(cond->get_params()[ID_BOOL_IN], "true");
    EXPECT_EQ(cond->get_operator()[ID_BOOL_IN], "==");
    EXPECT_TRUE(cond->useForTrigger());

    ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(0));
    ASSERT_NE(action, nullptr);
    ASSERT_EQ(action->get_size(), 1);
    EXPECT_EQ(action->get_output(0), io(ID_BOOL_OUT));
}

//A rule whose condition references an unknown IO is dropped at load time.
TEST_F(CoreSmokeTest, RuleWithUnknownIoIsDropped)
{
    loadConfig(minimalIoXml(),
               rulesXmlDocument(simpleRuleXml("Broken", "io_does_not_exist", "==",
                                              "true", ID_BOOL_OUT, "true")));

    EXPECT_EQ(ListeRule::Instance().size(), 1);
    Rule *rule = findRule("Broken");
    ASSERT_NE(rule, nullptr);
    //The rule object survives, but the unresolvable condition was not added
    EXPECT_EQ(rule->get_size_conds(), 0);
}

//End to end: changing the input fires the rule, which sets the output. This is
//the whole IOBase -> ListeRule -> Condition -> Action chain, synchronously.
TEST_F(CoreSmokeTest, RuleIsExecutedWhenTheInputChanges)
{
    loadConfig();

    IOBase *input = io(ID_BOOL_IN);
    IOBase *output = io(ID_BOOL_OUT);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);

    ASSERT_FALSE(output->get_value_bool());

    ASSERT_TRUE(input->set_value(true));

    EXPECT_TRUE(input->get_value_bool());
    EXPECT_TRUE(output->get_value_bool()) << "the rule did not run";
}

//Rules can also be evaluated by hand, without going through the IO signal.
TEST_F(CoreSmokeTest, RuleConditionsCanBeEvaluatedDirectly)
{
    loadConfig();

    Rule *rule = findRule(RULE_NAME);
    ASSERT_NE(rule, nullptr);

    EXPECT_FALSE(rule->CheckConditions());

    //Set the input without triggering the rules engine: use the value the
    //condition compares against
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(rule->CheckConditions());
}

//XML round trip: save, reload, and check that neither the IOs nor the file
//content moved.
TEST_F(CoreSmokeTest, IosSurviveAnXmlRoundTrip)
{
    loadConfig();

    EXPECT_TRUE(roundTripIo());

    //And the reloaded world is still usable
    ASSERT_EQ(ListeRoom::Instance().size(), 1);
    EXPECT_NE(io(ID_BOOL_IN), nullptr);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 4);
}

TEST_F(CoreSmokeTest, RulesSurviveAnXmlRoundTrip)
{
    loadConfig();

    EXPECT_TRUE(roundTripRules());

    ASSERT_EQ(ListeRule::Instance().size(), 1);
    Rule *rule = findRule(RULE_NAME);
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->get_size_conds(), 1);
    EXPECT_EQ(rule->get_size_actions(), 1);
}

//An IO created at runtime lands in the room, is findable by id, and survives a
//round trip; deleting it removes it from everywhere.
TEST_F(CoreSmokeTest, CreateAndDeleteIo)
{
    loadConfig();

    IOBase *created = createInternalIO("InternalBool", "io_created", "Created");
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(io("io_created"), created);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 5);
    EXPECT_EQ(firstRoom()->get_size(), 5);

    EXPECT_TRUE(roundTripIo());
    ASSERT_NE(io("io_created"), nullptr);
    EXPECT_EQ(io("io_created")->get_param("name"), "Created");

    //Pointers are invalidated by the round trip, look the IO up again
    ASSERT_TRUE(deleteIO(io("io_created")));
    EXPECT_EQ(io("io_created"), nullptr);
    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 4);
    EXPECT_EQ(firstRoom()->get_size(), 4);

    EXPECT_TRUE(roundTripIo());
    EXPECT_EQ(io("io_created"), nullptr);
}

//Deleting an IO used by a rule drops the rule as well.
TEST_F(CoreSmokeTest, DeletingAnIoDropsTheRulesUsingIt)
{
    loadConfig();

    ASSERT_EQ(ListeRule::Instance().size(), 1);
    ASSERT_TRUE(deleteIO(io(ID_BOOL_IN)));

    EXPECT_EQ(io(ID_BOOL_IN), nullptr);
    EXPECT_EQ(ListeRule::Instance().size(), 0);
}

//A rule built by hand behaves like a loaded one and is serialized identically.
TEST_F(CoreSmokeTest, RulesCanBeBuiltProgrammatically)
{
    loadConfig(minimalIoXml(), rulesXmlDocument(""));
    ASSERT_EQ(ListeRule::Instance().size(), 0);

    Rule *rule = addSimpleRule("Built", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true");
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(ListeRule::Instance().size(), 1);

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());

    EXPECT_TRUE(roundTripRules());
    EXPECT_NE(findRule("Built"), nullptr);

    //An unknown id gives no rule at all
    EXPECT_EQ(addSimpleRule("Bad", "nope", "==", "true", ID_BOOL_OUT, "true"), nullptr);
}

//Same through the XML factory path.
TEST_F(CoreSmokeTest, RulesCanBeAddedFromAnXmlFragment)
{
    loadConfig(minimalIoXml(), rulesXmlDocument(""));

    Rule *rule = addRuleFromXml(simpleRuleXml("FromXml", ID_INT, ">", "5",
                                              ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(rule->get_size_conds(), 1);
    EXPECT_EQ(rule->get_size_actions(), 1);

    EXPECT_EQ(addRuleFromXml("<not-a-rule/>"), nullptr);
}

//Several rooms, and a custom config built with the XML helpers.
TEST_F(CoreSmokeTest, SupportsSeveralRooms)
{
    std::string rooms;
    rooms += roomXml("Kitchen", "kitchen",
                     internalIoXml("InternalBool", "io_kitchen", "Kitchen light"), 2);
    rooms += roomXml("Garage", "garage",
                     internalIoXml("InternalInt", "io_garage", "Garage counter"), 7);

    loadConfig(ioXmlDocument(rooms), rulesXmlDocument(""));

    ASSERT_EQ(ListeRoom::Instance().size(), 2);
    ASSERT_NE(findRoom("Kitchen"), nullptr);
    EXPECT_EQ(findRoom("Kitchen")->get_hits(), 2);
    ASSERT_NE(findRoom("Garage"), nullptr);
    EXPECT_EQ(findRoom("Garage")->get_hits(), 7);

    EXPECT_EQ(ListeRoom::Instance().getRoomByIO(io("io_garage")), findRoom("Garage"));

    EXPECT_TRUE(roundTripIo());
    EXPECT_EQ(ListeRoom::Instance().size(), 2);
    EXPECT_NE(io("io_kitchen"), nullptr);
    EXPECT_NE(io("io_garage"), nullptr);
}

//A room added at runtime, with an IO created in it.
TEST_F(CoreSmokeTest, RoomsCanBeAddedAtRuntime)
{
    loadConfig();

    Room *room = addRoom("Cellar", "cellar");
    ASSERT_NE(room, nullptr);
    ASSERT_EQ(ListeRoom::Instance().size(), 2);

    IOBase *cellarIo = createInternalIO("InternalBool", "io_cellar", "Cellar", room);
    ASSERT_NE(cellarIo, nullptr);
    EXPECT_EQ(ListeRoom::Instance().getRoomByIO(cellarIo), room);

    EXPECT_TRUE(roundTripIo());
    ASSERT_NE(findRoom("Cellar"), nullptr);
    EXPECT_EQ(findRoom("Cellar")->get_size(), 1);
}

//The config directory really is private to the test, and Config reads and
//writes there and nowhere else.
TEST_F(CoreSmokeTest, ConfigIsIsolatedInATemporaryDirectory)
{
    EXPECT_EQ(Utils::getConfigFile(IO_CONFIG), configDir() + "/" IO_CONFIG);
    EXPECT_EQ(Utils::getCacheFile("iostates.cache"), cacheDir() + "/iostates.cache");

    loadConfig();
    saveConfig();

    EXPECT_NE(ioXmlOnDisk().find(ID_BOOL_IN), std::string::npos);
    EXPECT_NE(rulesXmlOnDisk().find(RULE_NAME), std::string::npos);
}

//An io.xml that does not exist yet is created empty by LoadConfigIO(), it must
//not be an error.
TEST_F(CoreSmokeTest, MissingConfigFilesAreCreated)
{
    Config::Instance().LoadConfigIO();
    Config::Instance().LoadConfigRule();

    EXPECT_EQ(ListeRoom::Instance().size(), 0);
    EXPECT_EQ(ListeRule::Instance().size(), 0);
    EXPECT_FALSE(ioXmlOnDisk().empty());
    EXPECT_FALSE(rulesXmlOnDisk().empty());
}
