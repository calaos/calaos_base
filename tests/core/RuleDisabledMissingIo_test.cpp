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

/******************************************************************************
 * E4.2e — a rule whose dependency is missing is DISABLED
 * ======================================================
 *
 * THE DANGER this file exists for. A rule is a CONJUNCTION. Until this ticket,
 * a condition naming an IO that no longer existed was rejected at load
 * (RulesFactory returned NULL, the condition was simply not added) and the
 * rule was loaded anyway, AMPUTATED - so it became MORE PERMISSIVE than what
 * the user wrote:
 *
 *     if presence == false AND time > 22h  ->  switch everything off
 *
 * delete the presence IO and the rule silently becomes
 *
 *     if time > 22h                        ->  switch everything off
 *
 * i.e. it fires every single night. The degenerate case (ALL the conditions
 * rejected) is harmless in comparison - CheckConditions() answers true for
 * zero condition, but no IO triggers the rule any more. It is the PARTIALLY
 * amputated rule that is dangerous.
 *
 * USER DECISION (2026-08-16): a rule with at least one condition or action
 * referencing an unresolvable IO must not execute at all, and must say so
 * loudly. Better an inert rule than one acting on incomplete criteria.
 *
 * What is pinned here:
 *
 *  1. THE CAUSE IS DISTINGUISHED. "IO not found" disables the rule; a
 *     malformed node or an unknown type is still a plain config error, the
 *     node is dropped and the rule keeps running exactly as before.
 *
 *  2. THE RULE IS DISABLED, NOT DROPPED, NOT AMPUTATED. It stays in
 *     ListeRule, it keeps ALL its conditions and actions, and it is excluded
 *     from execution on all four paths: trigger collection, CheckConditions(),
 *     CheckConditionsAsync() and ExecuteActions().
 *
 *  3. THE SAVE IS NON DESTRUCTIVE. The rejected condition and its parameters
 *     survive a save AND the save->reload->save round trip. This is the
 *     regression the E4.2c finding described: the id used to survive the save
 *     but the condition was rejected again on reload, so the rule came back
 *     with 0 condition and the id was lost at the NEXT save.
 *
 *  4. THE DIAGNOSIS. An error log naming the rule and the missing ids, plus a
 *     configuration alert (the channel used by the corrupt-config recovery)
 *     and ListeRule::getDisabledRules(), so a disabled rule is visible without
 *     reading the log file.
 *
 *  5. A HEALTHY RULE IS STRICTLY UNCHANGED.
 ******************************************************************************/

#include <iostream>
#include <sstream>

#include "CalaosCoreFixture.h"
#include "ActionStd.h"
#include "ConditionOutput.h"
#include "ConditionScript.h"
#include "ConditionStd.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const ID_GONE = "io_e42e_deleted_presence";

/* "if <inputId> == <inputValue> AND <secondId> == <secondValue> then
 *  <outputId> = true", both conditions being triggers.
 * The second input is the one the tests point at a deleted IO. */
std::string twoConditionRuleXml(const std::string &name,
                                const std::string &firstId,
                                const std::string &secondId,
                                const std::string &outputId)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"" << name << "\" type=\"rule\">\n"
       << "    <calaos:condition type=\"standard\" trigger=\"true\">\n"
       << "      <calaos:input id=\"" << firstId << "\" oper=\"==\" val=\"true\" />\n"
       << "    </calaos:condition>\n"
       << "    <calaos:condition type=\"standard\" trigger=\"true\">\n"
       << "      <calaos:input id=\"" << secondId << "\" oper=\"==\" val=\"false\" />\n"
       << "    </calaos:condition>\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << outputId << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";
    return ss.str();
}

//Capture what the logger prints (Logger writes to std::cout; gtest uses C
//stdio, so its own output is not swallowed by this).
class CoutCapture
{
public:
    CoutCapture(): saved(std::cout.rdbuf(captured.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(saved); }
    std::string str() const { return captured.str(); }

private:
    std::ostringstream captured;
    std::streambuf *saved;
};

}

class RuleDisabledMissingIoTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        //The tests read the output IOs to tell whether the actions ran, and
        //Config caches the value another test left there (CalaosCoreFixture.h)
        forgetIOState(ID_BOOL_OUT);
        forgetIOState(ID_STRING);
    }

    //The 2-condition rule of the ticket, second condition pointing at `secondId`
    void loadTwoConditionRule(const std::string &secondId)
    {
        loadConfig(minimalIoXml(),
                   rulesXmlDocument(twoConditionRuleXml("Presence", ID_BOOL_IN,
                                                        secondId, ID_BOOL_OUT)));
    }
};

/******************************************************************************
 * 1. Load: disabled, but neither dropped nor amputated
 ******************************************************************************/

TEST_F(RuleDisabledMissingIoTest, RuleWithAMissingIoIsLoadedDisabledAndComplete)
{
    loadTwoConditionRule(ID_GONE);

    //Still in the user's configuration
    ASSERT_EQ(ListeRule::Instance().size(), 1);
    Rule *rule = findRule("Presence");
    ASSERT_NE(rule, nullptr) << "the rule was dropped instead of being disabled";

    //NOT amputated: this is the whole point, the second condition is still
    //there with its operator and its value
    ASSERT_EQ(rule->get_size_conds(), 2)
        << "the unresolvable condition was dropped, the rule is amputated";
    EXPECT_EQ(rule->get_size_actions(), 1);

    ConditionStd *second = dynamic_cast<ConditionStd *>(rule->get_condition(1));
    ASSERT_NE(second, nullptr);
    ASSERT_EQ(second->get_size(), 1);
    EXPECT_EQ(second->get_input_id(0), ID_GONE);
    EXPECT_EQ(second->get_params()[ID_GONE], "false");
    EXPECT_EQ(second->get_operator()[ID_GONE], "==");
    //...and it resolves to nothing, which is exactly why the rule is disabled
    EXPECT_EQ(second->get_input(0), nullptr);

    //Disabled, and it says which id is missing
    EXPECT_TRUE(rule->isDisabled());
    ASSERT_EQ(rule->getMissingIoIds().size(), 1u);
    EXPECT_EQ(rule->getMissingIoIds()[0], ID_GONE);
    EXPECT_EQ(rule->getMissingIoDescription(), ID_GONE);

    //The first condition is untouched
    EXPECT_TRUE(second->hasMissingIo());
    EXPECT_FALSE(rule->get_condition(0)->hasMissingIo());
}

/******************************************************************************
 * 2. It never runs, even when the surviving condition is true
 ******************************************************************************/

//THE test of the ticket: the surviving half of the conjunction becomes true
//and the rule must NOT fire. Before E4.2e the amputated rule fired here.
TEST_F(RuleDisabledMissingIoTest, ADisabledRuleDoesNotFireWhenTheSurvivingConditionIsTrue)
{
    loadTwoConditionRule(ID_GONE);

    IOBase *input = io(ID_BOOL_IN);
    IOBase *output = io(ID_BOOL_OUT);
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_FALSE(output->get_value_bool());

    //This is the real signal path: set_value() -> ListeRule::ExecuteRuleSignal()
    ASSERT_TRUE(input->set_value(true));

    EXPECT_TRUE(input->get_value_bool());
    EXPECT_FALSE(output->get_value_bool())
        << "the disabled rule fired on its surviving condition alone";
}

//The same, from every execution entry point, because ListeRule reaches them
//directly and none of them may be the hole in the fence.
TEST_F(RuleDisabledMissingIoTest, EveryExecutionEntryPointRefuses)
{
    loadTwoConditionRule(ID_GONE);

    Rule *rule = findRule("Presence");
    ASSERT_NE(rule, nullptr);

    //Make the surviving condition true without going through the rules engine
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));

    EXPECT_FALSE(rule->CheckConditions());
    EXPECT_FALSE(rule->ExecuteActions());
    EXPECT_FALSE(rule->Execute());
    EXPECT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    bool called = false;
    bool result = true;
    rule->CheckConditionsAsync([&](bool check) { called = true; result = check; }, ID_BOOL_IN);
    EXPECT_TRUE(called) << "the callback must still be answered, with false";
    EXPECT_FALSE(result);
}

//No trigger at all: the rule is not even collected, so nothing can decide to
//run its actions later.
TEST_F(RuleDisabledMissingIoTest, ADisabledRuleIsNeverCollectedAsTriggered)
{
    loadTwoConditionRule(ID_GONE);

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));

    std::vector<Rule *> syncRules, asyncRules;
    ListeRule::Instance().collectTriggeredRules(ID_BOOL_IN, syncRules, asyncRules);

    EXPECT_TRUE(syncRules.empty());
    EXPECT_TRUE(asyncRules.empty());
}

//A rule with a ConditionStart is executed once at startup; a disabled one is
//not.
TEST_F(RuleDisabledMissingIoTest, ADisabledStartRuleIsNotExecutedAtStartup)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"Startup\" type=\"rule\">\n"
       << "    <calaos:condition type=\"start\" />\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << ID_GONE << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << ID_BOOL_OUT << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";

    loadConfig(minimalIoXml(), rulesXmlDocument(ss.str()));

    Rule *rule = findRule("Startup");
    ASSERT_NE(rule, nullptr);
    ASSERT_TRUE(rule->isDisabled());

    ListeRule::Instance().ExecuteStartRules();

    //The second action targets a perfectly valid IO: a per-action skip would
    //have run it. The whole rule is inert.
    EXPECT_FALSE(io(ID_BOOL_OUT)->get_value_bool());
}

/******************************************************************************
 * 3. The cause is distinguished from the other rejection causes
 ******************************************************************************/

//A malformed node (a script condition with no child at all) is refused by
//LoadFromXml() the way it always was: dropped, and the rule keeps running.
//Only "IO not found" disables.
TEST_F(RuleDisabledMissingIoTest, AMalformedNodeDropsTheNodeButDoesNotDisableTheRule)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"Malformed\" type=\"rule\">\n"
       << "    <calaos:condition type=\"standard\" trigger=\"true\">\n"
       << "      <calaos:input id=\"" << ID_BOOL_IN << "\" oper=\"==\" val=\"true\" />\n"
       << "    </calaos:condition>\n"
       //no <calaos:script> child: ConditionScript::LoadFromXml() answers false
       << "    <calaos:condition type=\"script\"></calaos:condition>\n"
       //unknown type: the factory builds nothing
       << "    <calaos:condition type=\"this-type-does-not-exist\" />\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << ID_BOOL_OUT << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";

    loadConfig(minimalIoXml(), rulesXmlDocument(ss.str()));

    Rule *rule = findRule("Malformed");
    ASSERT_NE(rule, nullptr);

    //Both unusable nodes were dropped, as before
    EXPECT_EQ(rule->get_size_conds(), 1);
    EXPECT_EQ(rule->get_size_actions(), 1);

    //...and the rule is NOT disabled: a corrupt node is a config error, not a
    //missing dependency
    EXPECT_FALSE(rule->isDisabled());
    EXPECT_TRUE(ListeRule::Instance().getDisabledRules().empty());

    //It still runs
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());
}

//An action, a ConditionOutput and a script trigger reach the same verdict.
TEST_F(RuleDisabledMissingIoTest, AMissingActionOutputDisablesTheRule)
{
    loadConfig(minimalIoXml(),
               rulesXmlDocument(simpleRuleXml("BadAction", ID_BOOL_IN, "==", "true",
                                              ID_GONE, "true")));

    Rule *rule = findRule("BadAction");
    ASSERT_NE(rule, nullptr);
    //Fatal: get_action(0) below indexes the vector without a bound check
    ASSERT_EQ(rule->get_size_actions(), 1) << "the action was dropped";
    EXPECT_TRUE(rule->isDisabled());

    ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(0));
    ASSERT_NE(action, nullptr);
    ASSERT_EQ(action->get_size(), 1);
    EXPECT_EQ(action->get_output_id(0), ID_GONE);
    EXPECT_EQ(action->get_params()[ID_GONE], "true");
}

TEST_F(RuleDisabledMissingIoTest, AMissingConditionOutputDisablesTheRule)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"BadOutputCond\" type=\"rule\">\n"
       << "    <calaos:condition type=\"output\" trigger=\"true\">\n"
       << "      <calaos:output id=\"" << ID_GONE << "\" oper=\"==\" val=\"true\" />\n"
       << "    </calaos:condition>\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << ID_BOOL_OUT << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";

    loadConfig(minimalIoXml(), rulesXmlDocument(ss.str()));

    Rule *rule = findRule("BadOutputCond");
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->get_size_conds(), 1) << "the condition was dropped";
    EXPECT_TRUE(rule->isDisabled());

    ConditionOutput *cond = dynamic_cast<ConditionOutput *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    EXPECT_EQ(cond->getOutputId(), ID_GONE);
    EXPECT_EQ(cond->get_params(), "true");
    EXPECT_EQ(cond->get_operator(), "==");
}

TEST_F(RuleDisabledMissingIoTest, AMissingScriptTriggerDisablesTheRuleAndKeepsTheId)
{
    std::ostringstream ss;
    ss << "  <calaos:rule name=\"Scripted\" type=\"rule\">\n"
       << "    <calaos:condition type=\"script\">\n"
       << "      <calaos:input id=\"" << ID_GONE << "\" />\n"
       << "      <calaos:script type=\"lua\"><![CDATA[return true]]></calaos:script>\n"
       << "    </calaos:condition>\n"
       << "    <calaos:action type=\"standard\">\n"
       << "      <calaos:output id=\"" << ID_BOOL_OUT << "\" val=\"true\" />\n"
       << "    </calaos:action>\n"
       << "  </calaos:rule>\n";

    loadConfig(minimalIoXml(), rulesXmlDocument(ss.str()));

    Rule *rule = findRule("Scripted");
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->get_size_conds(), 1);
    EXPECT_TRUE(rule->isDisabled());

    //The trigger id used to be thrown away here, which lost it at the next save
    ConditionScript *cond = dynamic_cast<ConditionScript *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_EQ(cond->getTriggerCount(), 1);
    EXPECT_EQ(cond->getTriggerId(0), ID_GONE);
}

/******************************************************************************
 * 4. The save is NON DESTRUCTIVE
 ******************************************************************************/

//The rejected condition, its operator and its value must still be in the file
//after a save: this is what stops the user from losing their rule for good.
TEST_F(RuleDisabledMissingIoTest, SavingKeepsTheUnresolvableConditionAndItsParameters)
{
    loadTwoConditionRule(ID_GONE);

    saveConfig();
    const std::string rules = rulesXmlOnDisk();

    EXPECT_NE(rules.find(ID_GONE), std::string::npos)
        << "the unresolvable reference was lost on save:\n" << rules;
    EXPECT_NE(rules.find(std::string("id=\"") + ID_GONE + "\" oper=\"==\" val=\"false\""),
              std::string::npos)
        << "the parameters of the unresolvable condition were lost on save:\n" << rules;

    //And the healthy half is of course still there
    EXPECT_NE(rules.find(ID_BOOL_IN), std::string::npos);
    EXPECT_NE(rules.find(ID_BOOL_OUT), std::string::npos);
}

/* THE round trip of the finding: save -> reload -> save. The id used to
 * survive the first save, but the reload rejected the condition again, so the
 * rule came back with 0 condition and the SECOND save wrote a rule the user
 * could never repair. roundTripRules() compares both the object graph and the
 * bytes of the file across the cycle. */
TEST_F(RuleDisabledMissingIoTest, ADisabledRuleSurvivesSaveReloadSaveUnchanged)
{
    loadTwoConditionRule(ID_GONE);
    ASSERT_TRUE(findRule("Presence")->isDisabled());

    EXPECT_TRUE(roundTripRules());

    //Pointers are invalidated by the round trip, look the rule up again
    Rule *rule = findRule("Presence");
    ASSERT_NE(rule, nullptr);
    //Fatal: get_condition(1) below indexes the vector without a bound check
    ASSERT_EQ(rule->get_size_conds(), 2);
    EXPECT_EQ(rule->get_size_actions(), 1);
    EXPECT_TRUE(rule->isDisabled()) << "the diagnosis was not recomputed at reload";

    ConditionStd *second = dynamic_cast<ConditionStd *>(rule->get_condition(1));
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->get_input_id(0), ID_GONE);
    EXPECT_EQ(second->get_params()[ID_GONE], "false");
    EXPECT_EQ(second->get_operator()[ID_GONE], "==");
}

//The rule runs again, untouched, as soon as the IO is back: the disabled state
//is derived from the config at every load, never written into it.
TEST_F(RuleDisabledMissingIoTest, TheRuleRunsAgainWhenTheMissingIoComesBack)
{
    loadTwoConditionRule(ID_GONE);
    ASSERT_TRUE(findRule("Presence")->isDisabled());

    //Save the rules as they stand, then reload with an io.xml that has the IO
    saveConfig();
    const std::string savedRules = rulesXmlOnDisk();

    std::string ios;
    ios += internalIoXml("InternalBool", ID_BOOL_IN, "Bool input");
    ios += internalIoXml("InternalBool", ID_BOOL_OUT, "Bool output");
    ios += internalIoXml("InternalInt", ID_INT, "Int value");
    ios += internalIoXml("InternalString", ID_STRING, "String value");
    ios += internalIoXml("InternalBool", ID_GONE, "Presence back");

    clearCoreState();
    forgetIOState(ID_BOOL_OUT);
    forgetIOState(ID_GONE);
    loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE, ios, 0)), savedRules);

    Rule *rule = findRule("Presence");
    ASSERT_NE(rule, nullptr);
    EXPECT_FALSE(rule->isDisabled()) << "the rule stayed disabled after the IO came back";
    ASSERT_EQ(rule->get_size_conds(), 2);

    //And it works: both conditions true -> the action runs
    ASSERT_NE(io(ID_GONE), nullptr);
    ASSERT_FALSE(io(ID_GONE)->get_value_bool()); //the second condition wants false
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());
}

/******************************************************************************
 * 5. The diagnosis: log, config alert, ListeRule view
 ******************************************************************************/

TEST_F(RuleDisabledMissingIoTest, TheDisablingIsLoggedWithTheRuleNameAndTheMissingId)
{
    std::string logged;
    {
        CoutCapture capture;
        loadTwoConditionRule(ID_GONE);
        logged = capture.str();
    }

    EXPECT_NE(logged.find("Presence"), std::string::npos)
        << "the log does not name the rule:\n" << logged;
    EXPECT_NE(logged.find(ID_GONE), std::string::npos)
        << "the log does not name the missing id:\n" << logged;
    EXPECT_NE(logged.find("DISABLED"), std::string::npos)
        << "the log does not say the rule is disabled:\n" << logged;
}

TEST_F(RuleDisabledMissingIoTest, DisabledRulesAreReportedThroughTheApiNotOnlyTheLog)
{
    size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    loadTwoConditionRule(ID_GONE);

    //Programmatic view, in `rules` order
    std::vector<Rule *> disabled = ListeRule::Instance().getDisabledRules();
    ASSERT_EQ(disabled.size(), 1u);
    EXPECT_EQ(disabled[0], findRule("Presence"));

    //Same channel as the corrupt-config recovery: queued at load, sent by
    //mail/push once the event loop is up
    const std::vector<std::string> &alerts = Config::Instance().getConfigAlerts();
    ASSERT_EQ(alerts.size(), alertsBefore + 1);
    EXPECT_NE(alerts.back().find("Presence"), std::string::npos);
    EXPECT_NE(alerts.back().find(ID_GONE), std::string::npos);
    EXPECT_NE(alerts.back().find("DISABLED"), std::string::npos);
}

/******************************************************************************
 * 6. A healthy rule is strictly unchanged
 ******************************************************************************/

TEST_F(RuleDisabledMissingIoTest, AHealthyRuleIsUntouched)
{
    size_t alertsBefore = Config::Instance().getConfigAlerts().size();

    loadConfig(); //the default minimal config, every id resolves

    Rule *rule = findRule(RULE_NAME);
    ASSERT_NE(rule, nullptr);
    EXPECT_FALSE(rule->isDisabled());
    EXPECT_TRUE(rule->getMissingIoIds().empty());
    EXPECT_TRUE(rule->getMissingIoDescription().empty());
    EXPECT_TRUE(ListeRule::Instance().getDisabledRules().empty());

    //No alert for a healthy configuration
    EXPECT_EQ(Config::Instance().getConfigAlerts().size(), alertsBefore);

    //It evaluates and fires exactly as before
    EXPECT_FALSE(rule->CheckConditions());
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(rule->CheckConditions());
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());

    EXPECT_TRUE(roundTripRules());
}

//A healthy rule sitting next to a disabled one keeps running: the verdict is
//per rule, it does not spread through the file.
TEST_F(RuleDisabledMissingIoTest, ADisabledRuleDoesNotDisableItsNeighbour)
{
    std::string rules;
    rules += twoConditionRuleXml("Broken", ID_BOOL_IN, ID_GONE, ID_STRING);
    rules += simpleRuleXml("Healthy", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true");

    loadConfig(minimalIoXml(), rulesXmlDocument(rules));

    ASSERT_EQ(ListeRule::Instance().size(), 2);
    ASSERT_NE(findRule("Broken"), nullptr);
    ASSERT_NE(findRule("Healthy"), nullptr);
    EXPECT_TRUE(findRule("Broken")->isDisabled());
    EXPECT_FALSE(findRule("Healthy")->isDisabled());

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));

    //The healthy rule ran...
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());
    //...and the disabled one did not (its action would have written here)
    EXPECT_EQ(io(ID_STRING)->get_value_string(), "");
}
