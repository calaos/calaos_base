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

//Rule/IO lifecycle tests (ticket T1.1).
//
//They cover what happens to the rules when an IO they reference is deleted:
//ListeRule::RemoveRule() used to look at ConditionStd and ActionStd only, so a
//rule referencing the IO through a ConditionOutput or a ConditionScript
//survived the deletion with a dangling IOBase*, which was then dereferenced at
//the next evaluation or at the next SaveConfigRule(). The ConditionStd path is
//already covered by the smoke test, everything else is here.
//
//The second half covers AutoScenario's step action indexing: getStepAction()
//skips the IOs driving the scenario itself while getStepActionCount() used a
//fixed "- 3", so an index could resolve to an empty ScenarioAction whose null
//`io` was dereferenced by getCategory() and Scenario::toJson().
//
//Run under ASan: a surviving rule shows up as a heap-use-after-free rather
//than a silently wrong result.

#include "CalaosCoreFixture.h"

#include "ActionStd.h"
#include "AutoScenario.h"
#include "ConditionOutput.h"
#include "ConditionScript.h"
#include "ConditionStd.h"
#include "Scenario.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//"if <outputId> <oper> <value> then <actionId> = <actionValue>", the condition
//being a ConditionOutput (type="output") instead of the usual ConditionStd.
std::string outputConditionRuleXml(const std::string &name,
                                   const std::string &outputId,
                                   const std::string &oper,
                                   const std::string &value,
                                   const std::string &actionId,
                                   const std::string &actionValue)
{
    std::ostringstream ss;
    ss << "<calaos:rule name=\"" << name << "\" type=\"rule\">\n"
       << "  <calaos:condition type=\"output\" trigger=\"true\">\n"
       << "    <calaos:output id=\"" << outputId << "\" oper=\"" << oper
       << "\" val=\"" << value << "\" />\n"
       << "  </calaos:condition>\n"
       << "  <calaos:action type=\"standard\">\n"
       << "    <calaos:output id=\"" << actionId << "\" val=\"" << actionValue << "\" />\n"
       << "  </calaos:action>\n"
       << "</calaos:rule>\n";

    return ss.str();
}

//A lua script condition triggered by <triggerId>. The script itself is never
//executed here, only its declared trigger IO matters.
std::string scriptConditionRuleXml(const std::string &name,
                                   const std::string &triggerId,
                                   const std::string &actionId,
                                   const std::string &actionValue)
{
    std::ostringstream ss;
    ss << "<calaos:rule name=\"" << name << "\" type=\"rule\">\n"
       << "  <calaos:condition type=\"script\">\n"
       << "    <calaos:input id=\"" << triggerId << "\" />\n"
       << "    <calaos:script type=\"lua\">return true</calaos:script>\n"
       << "  </calaos:condition>\n"
       << "  <calaos:action type=\"standard\">\n"
       << "    <calaos:output id=\"" << actionId << "\" val=\"" << actionValue << "\" />\n"
       << "  </calaos:action>\n"
       << "</calaos:rule>\n";

    return ss.str();
}

}

class RuleLifecycleTest: public CoreFixture
{
protected:
    //All tests start from the minimal IO set and no rule at all
    void loadIosOnly()
    {
        loadConfig(minimalIoXml(), rulesXmlDocument(""));
        ASSERT_EQ(ListeRule::Instance().size(), 0);
    }
};

/******************************************************************************
 * ListeRule::RemoveRule(): every condition/action type must be scanned
 ******************************************************************************/

//C1: the rule only knows ID_INT through its ConditionOutput. Before the fix
//RemoveRule() did not see it and the rule stayed in the list with a freed
//IOBase* in ConditionOutput::output.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedOnlyByAConditionOutputRemovesTheRule)
{
    loadIosOnly();

    Rule *rule = addRuleFromXml(outputConditionRuleXml("OutputCond", ID_INT, ">", "5",
                                                       ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->get_size_conds(), 1) << "the output condition was not loaded";

    ConditionOutput *cond = dynamic_cast<ConditionOutput *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_EQ(cond->getOutput(), io(ID_INT));

    ASSERT_EQ(ListeRule::Instance().size(), 1);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    EXPECT_EQ(ListeRule::Instance().size(), 0) << "the rule kept a dangling IOBase*";
    EXPECT_EQ(findRule("OutputCond"), nullptr);
    EXPECT_EQ(io(ID_INT), nullptr);
}

//C1: same through ConditionScript, which keeps its trigger IOs in a map keyed
//by pointer.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedOnlyByAConditionScriptRemovesTheRule)
{
    loadIosOnly();

    Rule *rule = addRuleFromXml(scriptConditionRuleXml("ScriptCond", ID_INT,
                                                       ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->get_size_conds(), 1);

    ConditionScript *cond = dynamic_cast<ConditionScript *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_TRUE(cond->containsTriggerIO(io(ID_INT)));

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    EXPECT_EQ(ListeRule::Instance().size(), 0) << "the rule kept a dangling IOBase*";
    EXPECT_EQ(findRule("ScriptCond"), nullptr);
}

//Deleting an IO must not take unrelated rules down with it.
TEST_F(RuleLifecycleTest, DeletingAnIoLeavesTheRulesThatDoNotUseItAlone)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("UsesInt", ID_INT, ">", "5",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addRuleFromXml(scriptConditionRuleXml("UsesString", ID_STRING,
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addSimpleRule("UsesBool", ID_BOOL_IN, "==", "true",
                            ID_BOOL_OUT, "true"), nullptr);

    ASSERT_EQ(ListeRule::Instance().size(), 3);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    EXPECT_EQ(ListeRule::Instance().size(), 2);
    EXPECT_EQ(findRule("UsesInt"), nullptr);
    EXPECT_NE(findRule("UsesString"), nullptr);
    EXPECT_NE(findRule("UsesBool"), nullptr);
}

//Several rules referencing the same IO through different condition types are
//all dropped in one pass (the removal loop erases while iterating).
TEST_F(RuleLifecycleTest, DeletingAnIoRemovesEveryRuleUsingItAtOnce)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("Out1", ID_INT, ">", "1",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addRuleFromXml(scriptConditionRuleXml("Script1", ID_INT,
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("Out2", ID_INT, "<", "9",
                                                    ID_BOOL_OUT, "false")), nullptr);
    //Referenced as the action target only
    ASSERT_NE(addSimpleRule("Action1", ID_BOOL_IN, "==", "true", ID_INT, "3"), nullptr);
    //Not referenced at all
    ASSERT_NE(addSimpleRule("Keep", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);

    ASSERT_EQ(ListeRule::Instance().size(), 5);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    ASSERT_EQ(ListeRule::Instance().size(), 1);
    EXPECT_NE(findRule("Keep"), nullptr);
}

//The first rule of the list being removed used to rely on an unsigned
//underflow to keep the iteration correct. Check the head of the list.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedByTheFirstRuleKeepsTheListConsistent)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("First", ID_INT, ">", "1",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addSimpleRule("Second", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);
    ASSERT_NE(addSimpleRule("Third", ID_BOOL_IN, "==", "false", ID_BOOL_OUT, "false"), nullptr);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    ASSERT_EQ(ListeRule::Instance().size(), 2);
    EXPECT_NE(findRule("Second"), nullptr);
    EXPECT_NE(findRule("Third"), nullptr);
}

//The acceptance criterion of the ticket: SaveConfigRule() right after the
//deletion must be clean. ConditionOutput::SaveToXml() dereferences its output
//and ConditionScript::SaveToXml() walks its trigger map, both of which used to
//read freed memory here.
TEST_F(RuleLifecycleTest, SavingRulesAfterAnIoDeletionIsClean)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("OutputCond", ID_INT, ">", "5",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addRuleFromXml(scriptConditionRuleXml("ScriptCond", ID_INT,
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addSimpleRule("Keep", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    saveConfig();

    const std::string rules = rulesXmlOnDisk();
    EXPECT_EQ(rules.find(ID_INT), std::string::npos)
        << "the deleted IO is still referenced in rules.xml:\n" << rules;
    EXPECT_NE(rules.find("Keep"), std::string::npos);

    //And the saved state still loads
    reloadFromDisk();
    EXPECT_EQ(ListeRule::Instance().size(), 1);
    EXPECT_NE(findRule("Keep"), nullptr);
}

//Deleting a whole room cascades into its IOs (Room::~Room -> deleteIO), so the
//same cleanup must happen there.
TEST_F(RuleLifecycleTest, DeletingARoomDropsTheRulesUsingItsIos)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("OutputCond", ID_INT, ">", "5",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addRuleFromXml(scriptConditionRuleXml("ScriptCond", ID_STRING,
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_EQ(ListeRule::Instance().size(), 2);

    ListeRoom::Instance().Remove(0);

    EXPECT_EQ(ListeRoom::Instance().get_io_count(), 0);
    EXPECT_EQ(ListeRule::Instance().size(), 0);
}

/******************************************************************************
 * AutoScenario step action indexing
 ******************************************************************************/

class AutoScenarioLifecycleTest: public CoreFixture
{
protected:
    AutoScenario *makeScenario(const std::string &scenarioId)
    {
        loadConfig(minimalIoXml(), rulesXmlDocument(""));

        Params p = { { "type", "Scenario" },
                     { "id", "io_" + scenarioId },
                     { "name", "Scenario " + scenarioId },
                     { "auto_scenario", scenarioId } };

        Scenario *sc = dynamic_cast<Scenario *>(createIO(p));
        if (!sc)
            return nullptr;

        AutoScenario *as = sc->getAutoScenario();
        if (as)
            as->checkScenarioRules();

        return as;
    }
};

//getStepActionCount() must only count the actions getStepAction() can return,
//otherwise the extra indexes resolve to an empty ScenarioAction.
TEST_F(AutoScenarioLifecycleTest, StepActionCountOnlyCountsReadableActions)
{
    AutoScenario *as = makeScenario("sc_count");
    ASSERT_NE(as, nullptr);

    as->addStep(1.0);
    ASSERT_EQ(as->getRuleSteps().size(), 1u);

    //A fresh step only holds the scenario machinery (step, timer value, timer
    //start), none of which is a user action
    EXPECT_EQ(as->getStepActionCount(0), 0);
    EXPECT_EQ(as->getStepAction(0, 0).io, nullptr);

    //A real action is reported
    as->addStepAction(0, io(ID_BOOL_OUT), "true");
    ASSERT_EQ(as->getStepActionCount(0), 1);
    EXPECT_EQ(as->getStepAction(0, 0).io, io(ID_BOOL_OUT));
    EXPECT_EQ(as->getStepAction(0, 0).action, "true");

    //An action targeting one of the scenario's own IOs is skipped by
    //getStepAction(), so it must not be counted either
    as->addStepAction(0, as->getIOScenario(), "false");
    EXPECT_EQ(as->getStepActionCount(0), 1)
        << "count and getStepAction() disagree, an index resolves to a null io";

    for (int i = 0;i < as->getStepActionCount(0);i++)
        EXPECT_NE(as->getStepAction(0, i).io, nullptr);
}

//Same for the end step, which JsonApi walks the same way.
TEST_F(AutoScenarioLifecycleTest, EndStepActionCountOnlyCountsReadableActions)
{
    AutoScenario *as = makeScenario("sc_end");
    ASSERT_NE(as, nullptr);
    ASSERT_NE(as->getRuleStepEnd(), nullptr);

    EXPECT_EQ(as->getEndStepActionCount(), 0);

    as->addStepAction(AutoScenario::END_STEP, io(ID_BOOL_OUT), "true");
    ASSERT_EQ(as->getEndStepActionCount(), 1);
    EXPECT_EQ(as->getEndStepAction(0).io, io(ID_BOOL_OUT));

    as->addStepAction(AutoScenario::END_STEP, as->getIOStep(), "-1");
    EXPECT_EQ(as->getEndStepActionCount(), 1);

    for (int i = 0;i < as->getEndStepActionCount();i++)
        EXPECT_NE(as->getEndStepAction(i).io, nullptr);
}

//getCategory() walked [0, getStepActionCount()[ and dereferenced sa.io without
//checking it: with a skip-listed action in the step it crashed on a null
//pointer.
TEST_F(AutoScenarioLifecycleTest, GetCategoryDoesNotCrashOnASkipListedAction)
{
    AutoScenario *as = makeScenario("sc_cat");
    ASSERT_NE(as, nullptr);

    as->addStep(1.0);
    ASSERT_EQ(as->getRuleSteps().size(), 1u);

    as->addStepAction(0, as->getIOScenario(), "false");
    as->addStepAction(0, as->getIOIsActive(), "false");

    EXPECT_EQ(as->getStepActionCount(0), 0);
    EXPECT_NO_FATAL_FAILURE(as->getCategory());
    EXPECT_EQ(as->getCategory(), "");

    //A real action is still categorized
    as->addStepAction(0, io(ID_BOOL_OUT), "true");
    EXPECT_EQ(as->getStepActionCount(0), 1);
    EXPECT_EQ(as->getCategory(), "other");
}

//Deleting an IO used by a scenario step must drop that step rule, whatever the
//condition types the scenario rules are built with.
TEST_F(AutoScenarioLifecycleTest, DeletingAnIoUsedByAStepDropsTheStepRule)
{
    AutoScenario *as = makeScenario("sc_del");
    ASSERT_NE(as, nullptr);

    as->addStep(1.0);
    as->addStepAction(0, io(ID_BOOL_OUT), "true");

    ASSERT_NE(findRule("sc_del_step"), nullptr);
    const int rulesBefore = ListeRule::Instance().size();

    ASSERT_TRUE(deleteIO(io(ID_BOOL_OUT)));

    EXPECT_EQ(ListeRule::Instance().size(), rulesBefore - 1);
    //AutoScenario::ruleSteps still holds the freed pointer, look the rule up by
    //name in ListeRule instead
    EXPECT_EQ(findRule("sc_del_step"), nullptr);
}
