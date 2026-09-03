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
                                   const std::string &actionValue,
                                   int conditionCount = 1)
{
    std::ostringstream ss;
    ss << "<calaos:rule name=\"" << name << "\" type=\"rule\">\n";

    //Several script conditions of the same rule can watch the same IO
    for (int i = 0;i < conditionCount;i++)
    {
        ss << "  <calaos:condition type=\"script\">\n"
           << "    <calaos:input id=\"" << triggerId << "\" />\n"
           << "    <calaos:script type=\"lua\">return true</calaos:script>\n"
           << "  </calaos:condition>\n";
    }

    ss << "  <calaos:action type=\"standard\">\n"
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
 *
 * T3.18 - THE CONTRACT OF THIS WHOLE SECTION CHANGED (user decision). Deleting
 * an IO no longer DESTROYS the rules that use it, it DISABLES them: the rule
 * stays in the list with all its conditions and actions, Rule::isDisabled()
 * answers true and it is never triggered, evaluated nor executed. Every case
 * below has been rewritten and renamed accordingly.
 *
 * What the section is actually about is UNCHANGED and still covered: every
 * condition/action type must be SEEN by RemoveRule(). It used to be observed as
 * "the rule disappeared"; it is now observed as "the rule is disabled and names
 * the id" - a strictly stronger assertion, since a rule that was not seen would
 * stay enabled AND keep its reference.
 ******************************************************************************/

//C1: the rule only knows ID_INT through its ConditionOutput. Before the fix
//RemoveRule() did not see it and the rule stayed in the list with a freed
//IOBase* in ConditionOutput::output.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedOnlyByAConditionOutputDisablesTheRule)
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

    ASSERT_EQ(ListeRule::Instance().size(), 1) << "the rule was destroyed, not disabled";
    ASSERT_NE(findRule("OutputCond"), nullptr);
    EXPECT_TRUE(findRule("OutputCond")->isDisabled()) << "RemoveRule() did not see the "
                                                         "ConditionOutput";
    EXPECT_EQ(findRule("OutputCond")->getMissingIoDescription(), ID_INT);
    EXPECT_EQ(io(ID_INT), nullptr);
}

//C1: same through ConditionScript, which keeps its trigger IOs in a map keyed
//by pointer.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedOnlyByAConditionScriptDisablesTheRule)
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

    ASSERT_EQ(ListeRule::Instance().size(), 1) << "the rule was destroyed, not disabled";
    ASSERT_NE(findRule("ScriptCond"), nullptr);
    EXPECT_TRUE(findRule("ScriptCond")->isDisabled()) << "RemoveRule() did not see the "
                                                         "ConditionScript";
    EXPECT_EQ(findRule("ScriptCond")->getMissingIoDescription(), ID_INT);
}

//Deleting an IO must not disable unrelated rules.
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

    //T3.18: nothing is erased any more, so the count does not move at all
    EXPECT_EQ(ListeRule::Instance().size(), 3);
    ASSERT_NE(findRule("UsesInt"), nullptr);
    EXPECT_TRUE(findRule("UsesInt")->isDisabled());
    EXPECT_FALSE(findRule("UsesString")->isDisabled());
    EXPECT_FALSE(findRule("UsesBool")->isDisabled());
}

//Several rules referencing the same IO through different condition types are
//all caught in one pass.
TEST_F(RuleLifecycleTest, DeletingAnIoDisablesEveryRuleUsingItAtOnce)
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

    ASSERT_EQ(ListeRule::Instance().size(), 5);
    for (const char *name: { "Out1", "Script1", "Out2", "Action1" })
    {
        ASSERT_NE(findRule(name), nullptr) << name;
        EXPECT_TRUE(findRule(name)->isDisabled()) << name << " was not disabled";
    }
    EXPECT_FALSE(findRule("Keep")->isDisabled());
}

//The head of the list is the index the old destroying loop used to get wrong
//(it relied on an unsigned underflow). Nothing is erased now, so the check is
//that the ORDER and the contents are strictly untouched.
TEST_F(RuleLifecycleTest, DeletingAnIoUsedByTheFirstRuleKeepsTheListConsistent)
{
    loadIosOnly();

    ASSERT_NE(addRuleFromXml(outputConditionRuleXml("First", ID_INT, ">", "1",
                                                    ID_BOOL_OUT, "true")), nullptr);
    ASSERT_NE(addSimpleRule("Second", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);
    ASSERT_NE(addSimpleRule("Third", ID_BOOL_IN, "==", "false", ID_BOOL_OUT, "false"), nullptr);

    ASSERT_TRUE(deleteIO(io(ID_INT)));

    ASSERT_EQ(ListeRule::Instance().size(), 3);
    EXPECT_EQ(ListeRule::Instance().get_rule(0)->get_name(), "First");
    EXPECT_EQ(ListeRule::Instance().get_rule(1)->get_name(), "Second");
    EXPECT_EQ(ListeRule::Instance().get_rule(2)->get_name(), "Third");
    EXPECT_TRUE(ListeRule::Instance().get_rule(0)->isDisabled());
    EXPECT_FALSE(ListeRule::Instance().get_rule(1)->isDisabled());
    EXPECT_FALSE(ListeRule::Instance().get_rule(2)->isDisabled());
}

/* SaveConfigRule() right after the deletion must be clean - the acceptance
 * criterion of T1.1, still met: ConditionOutput::SaveToXml() and
 * ConditionScript::SaveToXml() used to read freed memory here.
 *
 * T3.18 - CONTRACT CHANGED, and the case renamed (it used to be
 * SavingRulesAfterAnIoDeletionIsClean and asserted that the deleted id was GONE
 * from rules.xml). The save is now deliberately NON DESTRUCTIVE: the dead id is
 * written back verbatim, which is what E4.2e's mechanism needs to reproduce the
 * disabled state at the next load. Losing it would be losing the user's rule.
 */
TEST_F(RuleLifecycleTest, SavingRulesAfterAnIoDeletionKeepsTheDeadReferenceAndReloads)
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
    EXPECT_NE(rules.find(ID_INT), std::string::npos)
        << "the deleted IO was dropped from rules.xml, the user's rule is lost:\n" << rules;
    EXPECT_NE(rules.find("Keep"), std::string::npos);

    //And the saved state still loads - all three rules, the two of them still
    //disabled because the load path re-detects the very same missing id
    reloadFromDisk();
    EXPECT_EQ(ListeRule::Instance().size(), 3);
    ASSERT_NE(findRule("Keep"), nullptr);
    EXPECT_FALSE(findRule("Keep")->isDisabled());
    ASSERT_NE(findRule("OutputCond"), nullptr);
    EXPECT_TRUE(findRule("OutputCond")->isDisabled());
    ASSERT_NE(findRule("ScriptCond"), nullptr);
    EXPECT_TRUE(findRule("ScriptCond")->isDisabled());
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

/* Deleting an IO used by a scenario step KEEPS that step rule and disables the
 * whole scenario, whatever the condition types the scenario rules are built
 * with.
 *
 * T3.18 - CONTRACT CHANGED (user decision), and the case renamed with it (it
 * used to be DeletingAnIoUsedByAStepDropsTheStepRule and asserted the step was
 * destroyed). Amputating a scenario made it run a sequence the user never
 * wrote, silently; it is now disabled whole, and stays disabled until it is
 * re-enabled by hand. tests/core/ScenarioDisabledMissingIo_test.cpp is where
 * that is covered end to end - this case only pins the ListeRule side of it.
 */
TEST_F(AutoScenarioLifecycleTest, DeletingAnIoUsedByAStepDisablesTheStepRule)
{
    AutoScenario *as = makeScenario("sc_del");
    ASSERT_NE(as, nullptr);

    as->addStep(1.0);
    as->addStepAction(0, io(ID_BOOL_OUT), "true");

    ASSERT_NE(findRule("sc_del_step"), nullptr);
    const int rulesBefore = ListeRule::Instance().size();

    ASSERT_TRUE(deleteIO(io(ID_BOOL_OUT)));

    //nothing was erased: the step rule and every other rule of the scenario are
    //still there, in their original order
    EXPECT_EQ(ListeRule::Instance().size(), rulesBefore);
    ASSERT_NE(findRule("sc_del_step"), nullptr);
    EXPECT_TRUE(findRule("sc_del_step")->isDisabled());
    EXPECT_EQ(findRule("sc_del_step")->getMissingIoDescription(), ID_BOOL_OUT);

    //the step is still readable, with its pause - it is the SCENARIO that is
    //disabled, not the description of the step that is destroyed
    ASSERT_EQ(as->getRuleSteps().size(), 1u);
    EXPECT_EQ(as->getStepPause(0), 1.0);

    //The pointer based accessors still drop the action whose IO is gone -
    //they resolve, and there is nothing to resolve to. The DEFINITION keeps
    //it, which is what the payload renders.
    EXPECT_EQ(as->getStepActionCount(0), 0);
    EXPECT_EQ(as->getStepAction(0, 0).io, nullptr);
    EXPECT_EQ(as->getCategory(), "");

    //and the scenario is disabled, live and persisted
    EXPECT_TRUE(as->isBroken());
    EXPECT_TRUE(as->isDisabledMissingIo());

    //Writing through the same index still works on the surviving rule
    as->setStepPause(0, 2.0);
    as->addStepAction(0, io(ID_INT), "5");
    EXPECT_EQ(as->getStepPause(0), 2.0);
}

//The banal exploit of the dangling step: scenario B is used as the action of a
//step of scenario A, then B is deleted (scenario_del). Deleting B's IO drops
//A's step rule, and the first get_scenarios reads A through Scenario::toJson().
TEST_F(AutoScenarioLifecycleTest, DeletingAScenarioUsedAsAStepActionIsSafeToSerialize)
{
    AutoScenario *as = makeScenario("sc_a");
    ASSERT_NE(as, nullptr);

    Scenario *scA = dynamic_cast<Scenario *>(io("io_sc_a"));
    ASSERT_NE(scA, nullptr);

    //Scenario B, the one the user will delete
    Params pb = { { "type", "Scenario" },
                  { "id", "io_sc_b" },
                  { "name", "Scenario sc_b" },
                  { "auto_scenario", "sc_b" } };
    Scenario *scB = dynamic_cast<Scenario *>(createIO(pb));
    ASSERT_NE(scB, nullptr);
    ASSERT_NE(scB->getAutoScenario(), nullptr);
    ASSERT_TRUE(scB->getAutoScenario()->checkScenarioRules());

    //A step of A starts B
    as->addStep(1.0);
    as->addStepAction(0, scB, "true");
    ASSERT_EQ(as->getRuleSteps().size(), 1u);
    ASSERT_EQ(as->getStepActionCount(0), 1);

    /* scenario_del on B: its IO goes away.
     * T3.18 - CONTRACT CHANGED. A's step rule used to be DESTROYED with it,
     * which is what made the serialization of A a use-after-free hazard in the
     * first place (E4.2f). It is now KEPT and disabled, so the hazard cannot
     * even arise on this path - and A is disabled whole instead of silently
     * losing a step. The case still does what it is here for: serialize A
     * right after B's deletion and check nothing blows up.
     */
    ASSERT_TRUE(deleteIO(io("io_sc_b")));
    ASSERT_NE(findRule("sc_a_step"), nullptr);
    EXPECT_TRUE(findRule("sc_a_step")->isDisabled());
    EXPECT_TRUE(as->isDisabledMissingIo());

    //get_scenarios on the survivor
    EXPECT_EQ(as->getRuleSteps().size(), 1u);

    /* THE PAYLOAD KEEPS THE DEAD ACTION, and that is the point of naming IOs
     * by id: the step still carries it, marked resolved="false", so a client
     * that reads this scenario back and sends it again writes it back instead
     * of erasing it.
     */
    const Json jret = scA->toJson();
    ASSERT_TRUE(jret.is_object());
    ASSERT_TRUE(jret["steps"].is_array());
    ASSERT_EQ(jret["steps"].size(), 1u) << jret.dump();

    ASSERT_EQ(jret["steps"][0]["actions"].size(), 1u) << jret.dump();
    EXPECT_EQ(jret["steps"][0]["actions"][0].value("io", std::string()), "io_sc_b");
    EXPECT_EQ(jret["steps"][0]["actions"][0].value("resolved", std::string()), "false");

    //the final step is a field of its own, and it is empty here
    ASSERT_TRUE(jret["final_step"].is_object());
    EXPECT_EQ(jret["final_step"]["actions"].size(), 0u);

    EXPECT_EQ(jret.value("broken", std::string()), "true");
    EXPECT_EQ(jret.value("disabled_missing_io", std::string()), "true");
    EXPECT_EQ(jret.value("missing_ios", std::string()), "io_sc_b");
}

/******************************************************************************
 * ListeRule execution: dispatch of the script conditions, execution lock
 ******************************************************************************/

/* ExecuteRuleSignal() cannot be observed through the singleton here: script
 * conditions are evaluated by detached lua processes and no libuv loop runs in
 * the tests (see CalaosCoreFixture.h). This subclass drives the very same code
 * on its own rule list and replaces the dispatch by a counter, which is exactly
 * where what is covered below happens: how many asynchronous evaluations a
 * single event starts, and in which state the execution lock is left once the
 * march is over.
 */
class ProbeListeRule: public ListeRule
{
public:
    //One entry per asynchronous evaluation started, in dispatch order
    std::vector<Rule *> dispatched;

    //Called from the middle of a march, when a rule is dispatched
    std::function<void ()> onDispatch;

    //Replays the completion of an asynchronous evaluation
    using ListeRule::asyncConditionsChecked;

protected:
    void dispatchAsyncRule(Rule *rule, const std::string &id) override
    {
        VAR_UNUSED(id);

        dispatched.push_back(rule);

        if (onDispatch)
            onDispatch();
    }
};

namespace
{

//Same sequence as Config::LoadConfigRule(), but the rule is added to `list`
//instead of the singleton (which owns and deletes it)
Rule *addRuleTo(ListeRule &list, const std::string &ruleXml)
{
    pugi::xml_document document;
    if (!document.load_string(ruleXml.c_str()))
        return nullptr;

    pugi::xml_node node = document.document_element();
    if (!node || std::string(node.name()) != "calaos:rule" ||
        !node.attribute("name") || !node.attribute("type"))
        return nullptr;

    Rule *rule = new Rule(node.attribute("type").as_string(), node.attribute("name").as_string());
    rule->LoadFromXml(node);
    list.Add(rule);

    return rule;
}

}

class RuleDispatchTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();

        //The tests below read the output IO to tell whether the actions ran, and
        //Config caches the value another test left there (see CalaosCoreFixture.h)
        forgetIOState(ID_BOOL_OUT);

        loadConfig(minimalIoXml(), rulesXmlDocument(""));
    }
};

//The asynchronous evaluation runs *every* script condition of the rule, so one
//dispatch per rule and per event is enough. Dispatching once per matching
//condition spawned the scripts N times and executed the actions N times.
TEST_F(RuleDispatchTest, ARuleWithSeveralScriptConditionsIsDispatchedOnce)
{
    ProbeListeRule rules;

    ASSERT_NE(addRuleTo(rules, scriptConditionRuleXml("TwoScripts", ID_INT,
                                                      ID_BOOL_OUT, "true", 2)), nullptr);
    ASSERT_EQ(rules.get_rule(0)->get_size_conds(), 2);

    rules.ExecuteRuleSignal(ID_INT);

    EXPECT_EQ(rules.dispatched.size(), 1u)
        << "one asynchronous evaluation per matching condition instead of per rule";
}

//Same, from the collect side: a rule appears once in each list whatever the
//number of its conditions matching the trigger.
TEST_F(RuleDispatchTest, CollectReportsEachRuleOnce)
{
    ProbeListeRule rules;

    Rule *scriptRule = addRuleTo(rules, scriptConditionRuleXml("Scripts", ID_INT,
                                                               ID_BOOL_OUT, "true", 3));
    ASSERT_NE(scriptRule, nullptr);

    std::vector<Rule *> syncRules, asyncRules;
    rules.collectTriggeredRules(ID_INT, syncRules, asyncRules);

    EXPECT_TRUE(syncRules.empty());
    ASSERT_EQ(asyncRules.size(), 1u);
    EXPECT_EQ(asyncRules[0], scriptRule);

    //An unrelated IO triggers nothing
    syncRules.clear();
    asyncRules.clear();
    rules.collectTriggeredRules(ID_STRING, syncRules, asyncRules);
    EXPECT_TRUE(syncRules.empty());
    EXPECT_TRUE(asyncRules.empty());
}

//Every rule watching the IO gets its own dispatch
TEST_F(RuleDispatchTest, EveryScriptRuleWatchingTheIoIsDispatched)
{
    ProbeListeRule rules;

    Rule *first = addRuleTo(rules, scriptConditionRuleXml("First", ID_INT,
                                                          ID_BOOL_OUT, "true", 2));
    Rule *second = addRuleTo(rules, scriptConditionRuleXml("Second", ID_INT,
                                                           ID_BOOL_OUT, "false"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    //Watches another IO
    ASSERT_NE(addRuleTo(rules, scriptConditionRuleXml("Other", ID_STRING,
                                                      ID_BOOL_OUT, "true")), nullptr);

    rules.ExecuteRuleSignal(ID_INT);

    ASSERT_EQ(rules.dispatched.size(), 2u);
    EXPECT_EQ(rules.dispatched[0], first);
    EXPECT_EQ(rules.dispatched[1], second);
}

//The core of the wedge: the lock used to be held until the last script callback
//came back. A script that never completes (spawn failure, hung process) then
//locked the engine for good, and every signal received meanwhile re-armed an
//idler that re-scheduled itself at every loop iteration.
TEST_F(RuleDispatchTest, APendingScriptDoesNotHoldTheExecutionLock)
{
    ProbeListeRule rules;

    ASSERT_NE(addRuleTo(rules, scriptConditionRuleXml("Script", ID_INT,
                                                      ID_BOOL_OUT, "true")), nullptr);

    rules.ExecuteRuleSignal(ID_INT);

    ASSERT_EQ(rules.dispatched.size(), 1u);
    EXPECT_FALSE(rules.isExecutionLocked())
        << "the engine stays locked until a script that may never answer comes back";
    EXPECT_EQ(rules.pendingTriggerCount(), 0u);

    //And the next events are executed, not deferred to an idler
    rules.ExecuteRuleSignal(ID_INT);
    rules.ExecuteRuleSignal(ID_INT);

    EXPECT_EQ(rules.dispatched.size(), 3u);
    EXPECT_FALSE(rules.isExecutionLocked());
}

//Re-entrancy is still refused: a signal received while a march is running is
//queued, not executed on top of it. It is then executed by the running march,
//without any idler in between.
TEST_F(RuleDispatchTest, ATriggerSignalledDuringAMarchIsDeferredThenExecuted)
{
    ProbeListeRule rules;

    Rule *first = addRuleTo(rules, scriptConditionRuleXml("First", ID_INT,
                                                          ID_BOOL_OUT, "true"));
    Rule *second = addRuleTo(rules, scriptConditionRuleXml("Second", ID_STRING,
                                                           ID_BOOL_OUT, "true"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    bool reentered = false;
    size_t pendingSeen = 0;
    bool lockedSeen = false;

    rules.onDispatch = [&]()
    {
        if (reentered)
            return;
        reentered = true;

        //An action of the running march changed an IO, which signals back here
        rules.ExecuteRuleSignal(ID_STRING);

        lockedSeen = rules.isExecutionLocked();
        pendingSeen = rules.pendingTriggerCount();
    };

    rules.ExecuteRuleSignal(ID_INT);

    EXPECT_TRUE(lockedSeen) << "the nested signal was executed on top of the running march";
    EXPECT_EQ(pendingSeen, 1u);

    ASSERT_EQ(rules.dispatched.size(), 2u);
    EXPECT_EQ(rules.dispatched[0], first);
    EXPECT_EQ(rules.dispatched[1], second) << "the deferred trigger was never executed";

    EXPECT_EQ(rules.pendingTriggerCount(), 0u);
    EXPECT_FALSE(rules.isExecutionLocked());
}

/* The rule can be destroyed while its scripts are still running, and nothing
 * cancels a ScriptExec callback. The callback used to run
 * rule->ExecuteActions() on freed memory (heap use after free under ASan).
 *
 * T3.18 - the WAY the rule is destroyed changed, not what is covered. Deleting
 * an IO no longer destroys the rules using it (it disables them), so the
 * destruction is now reached through the room, which is still a Destroy path
 * (Room::~Room). The dangling-callback hazard is unchanged and still real:
 * ListeRule::Remove(), room deletion and AutoScenario::deleteRules() all
 * destroy rules under a running script.
 */
TEST_F(RuleDispatchTest, ScriptCompletionOnADestroyedRuleIsIgnored)
{
    Rule *rule = addRuleFromXml(scriptConditionRuleXml("Async", ID_INT,
                                                       ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);

    std::weak_ptr<bool> token = rule->aliveToken();
    EXPECT_FALSE(token.expired());

    ASSERT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    //Destroying the room destroys its IOs and, with them, the rules using them
    ListeRoom::Instance().Remove(0);
    ASSERT_EQ(ListeRule::Instance().size(), 0);
    EXPECT_TRUE(token.expired());

    //The detached script finally answers: `rule` is dangling, the callback must
    //not touch it nor execute anything
    ProbeListeRule rules;
    rules.asyncConditionsChecked(rule, token, true);

    EXPECT_FALSE(rules.isExecutionLocked());
}

/* T3.18 - THE NEW HALF of the same hazard. A rule surviving the deletion of one
 * of its IOs is exactly what this ticket introduces, so a script started before
 * the deletion now lands on a rule that is ALIVE and DISABLED - a state that
 * did not exist on this path before. It must not act: executeActionsLocked()
 * goes through Rule::ExecuteActions(), which refuses for a disabled rule
 * (E4.2e). Without this case, T3.18 would have opened a way to run the actions
 * of a rule whose criteria are incomplete.
 */
TEST_F(RuleDispatchTest, ScriptCompletionOnARuleDisabledMeanwhileDoesNotAct)
{
    Rule *rule = addRuleFromXml(scriptConditionRuleXml("Async", ID_INT,
                                                       ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);

    std::weak_ptr<bool> token = rule->aliveToken();
    ASSERT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    //the IO the script watches goes away while the script is running
    ASSERT_TRUE(deleteIO(io(ID_INT)));
    ASSERT_EQ(ListeRule::Instance().size(), 1) << "the rule was destroyed, not disabled";
    ASSERT_FALSE(token.expired());
    ASSERT_TRUE(rule->isDisabled());

    ProbeListeRule rules;
    rules.asyncConditionsChecked(rule, token, true);

    EXPECT_FALSE(io(ID_BOOL_OUT)->get_value_bool())
        << "the actions of a disabled rule were executed by a late script callback";
    EXPECT_FALSE(rules.isExecutionLocked());
}

//A rule that is still there runs its actions when its scripts pass
TEST_F(RuleDispatchTest, ScriptCompletionOnALiveRuleExecutesTheActions)
{
    ProbeListeRule rules;

    Rule *rule = addRuleTo(rules, scriptConditionRuleXml("Async", ID_INT,
                                                         ID_BOOL_OUT, "true"));
    ASSERT_NE(rule, nullptr);
    ASSERT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    rules.asyncConditionsChecked(rule, rule->aliveToken(), false);
    EXPECT_FALSE(io(ID_BOOL_OUT)->get_value_bool()) << "conditions failed, actions ran anyway";

    rules.asyncConditionsChecked(rule, rule->aliveToken(), true);
    EXPECT_TRUE(io(ID_BOOL_OUT)->get_value_bool());

    //The lock is released again, whatever the path
    EXPECT_FALSE(rules.isExecutionLocked());
}
