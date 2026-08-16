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

//E4.2d - explicit and unique ownership of the rules (step 4/6 of the E4.2
//series), the rule-side counterpart of E4.2b's Room/ListeRoom conversion.
//
//The model this file pins:
//  - a Rule is the ONE owner of its Conditions and Actions
//    (vector<unique_ptr<...>>), destroyed through the base class pointer, so
//    the DERIVED destructor is the one that runs;
//  - ListeRule is the ONE owner of the Rules (vector<unique_ptr<Rule>>);
//    rules_scenarios is a NON-OWNING index over the same objects;
//  - every accessor (get_condition/get_action/get_rule/operator[]/
//    collectTriggeredRules/getRuleAutoScenario) hands out NON-OWNING raw
//    pointers: the object stays in place and stays usable;
//  - there is no hand-back path (the analogue of E4.2b's delete_io(io,
//    del=false)): nothing in the tree ever takes a Condition, an Action or a
//    Rule back out of its owner, so nothing here is a release(). What is
//    pinned instead is that a removal really destroys, exactly once;
//  - removing the same object twice is a no-op, never a double free.
//
//The ORDER invariant of the series (the order of `rules` is the rule
//evaluation order, the order of conds/actions is the evaluation/execution
//order) is pinned here on the removal paths; the dispatch-order tests of
//E4.2a/E4.2c (core/RuleIoReference_test.cpp) are left untouched.
//
//"Was it really destroyed?" is answered without a leak checker by two oracles:
//Rule::aliveToken(), a weak_ptr that expires with the Rule, and the traced
//Condition/Action below, which append to a shared log when their destructor
//runs.

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Action.h"
#include "CalaosCoreFixture.h"
#include "Condition.h"
#include "ListeRule.h"
#include "Rule.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//Destruction log shared by the traced objects of one test
std::vector<std::string> destroyLog;

/* A Condition whose destructor is observable. Held by the Rule as a
 * unique_ptr<Condition>: if the deletion were not polymorphic, this body would
 * never run and every EXPECT below would fail.
 */
class TracedCondition: public Condition
{
public:
    explicit TracedCondition(const std::string &tag):
        Condition(COND_UNKONWN), name(tag)
    { }

    ~TracedCondition() override { destroyLog.push_back("cond:" + name); }

    bool Evaluate() override { return evaluates; }

    std::string name;
    bool evaluates = true;
};

//Same for an Action
class TracedAction: public Action
{
public:
    explicit TracedAction(const std::string &tag):
        Action(ACTION_UNKONWN), name(tag)
    { }

    ~TracedAction() override { destroyLog.push_back("action:" + name); }

    bool Execute() override
    {
        executed++;
        return true;
    }

    std::string name;
    int executed = 0;
};

//A rule list of its own, so that a test can destroy the whole container
//without touching the singleton (whose lifetime is the process). ListeRule's
//constructor is protected, exactly like ListeRoom's.
class OwnedListeRule: public ListeRule
{
public:
    OwnedListeRule() { }
};

//Build "name" with `condCount` traced conditions and `actionCount` traced
//actions, all tagged "<name>.<i>". Not added to any list.
Rule *tracedRule(const std::string &name, int condCount, int actionCount)
{
    Rule *rule = new Rule("rule", name);

    for (int i = 0; i < condCount; i++)
        rule->AddCondition(new TracedCondition(name + "." + std::to_string(i)));
    for (int i = 0; i < actionCount; i++)
        rule->AddAction(new TracedAction(name + "." + std::to_string(i)));

    return rule;
}

//Names of the rules currently held by `list`, in list order
std::vector<std::string> ruleOrder(ListeRule &list)
{
    std::vector<std::string> names;
    for (int i = 0; i < list.size(); i++)
        names.push_back(list.get_rule(i)->get_name());
    return names;
}

}

class RuleOwnershipTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        destroyLog.clear();
    }

    void TearDown() override
    {
        destroyLog.clear();
        CoreFixture::TearDown();
    }
};

/******************************************************************************
 * Rule owns its conditions and its actions
 ******************************************************************************/

//The base case: destroying a Rule destroys everything it holds, once, and
//through the derived destructor.
TEST_F(RuleOwnershipTest, DestroyingARuleDestroysItsConditionsAndActions)
{
    delete tracedRule("r", 2, 3);

    EXPECT_EQ(destroyLog.size(), 5u);
    EXPECT_EQ(std::count(destroyLog.begin(), destroyLog.end(), "cond:r.0"), 1);
    EXPECT_EQ(std::count(destroyLog.begin(), destroyLog.end(), "action:r.2"), 1);
}

//The destruction order the tree has always had: conditions first, front to
//back, then actions. Members alone would give the opposite (reverse
//declaration order), which is why ~Rule() still has a body.
TEST_F(RuleOwnershipTest, DestructionOrderIsConditionsThenActionsFrontToBack)
{
    delete tracedRule("r", 2, 2);

    const std::vector<std::string> expected = {
        "cond:r.0", "cond:r.1", "action:r.0", "action:r.1"
    };
    EXPECT_EQ(destroyLog, expected);
}

//get_condition()/get_action() are NON-OWNING observations: the pointer handed
//out is the one that was added, the Rule keeps it, and it is still usable.
TEST_F(RuleOwnershipTest, AccessorsHandOutTheStoredObjectWithoutGivingItUp)
{
    std::unique_ptr<Rule> rule(new Rule("rule", "observe"));

    TracedCondition *cond = new TracedCondition("c");
    TracedAction *act = new TracedAction("a");
    rule->AddCondition(cond);
    rule->AddAction(act);

    EXPECT_EQ(rule->get_condition(0), cond);
    EXPECT_EQ(rule->get_action(0), act);

    //Observing does not destroy anything...
    EXPECT_TRUE(destroyLog.empty());

    //...and the object is live: the rule runs it through the same pointers.
    EXPECT_TRUE(rule->Execute());
    EXPECT_EQ(act->executed, 1);

    rule.reset();
    EXPECT_EQ(destroyLog.size(), 2u);
}

//RemoveCondition() destroys the condition (the raw pointer version erased it
//without deleting it, i.e. leaked it - no caller, so nothing observed the
//difference) and keeps the ORDER of the others, which is their evaluation
//order.
TEST_F(RuleOwnershipTest, RemoveConditionDestroysItAndKeepsTheOrderOfTheOthers)
{
    std::unique_ptr<Rule> rule(new Rule("rule", "r"));
    rule->AddCondition(new TracedCondition("first"));
    rule->AddCondition(new TracedCondition("middle"));
    rule->AddCondition(new TracedCondition("last"));

    rule->RemoveCondition(1);

    ASSERT_EQ(rule->get_size_conds(), 2);
    EXPECT_EQ(destroyLog, std::vector<std::string>{ "cond:middle" });

    //The survivors are still the right objects, in the right order: an erase
    //that destroyed mid-shuffle would have left a null slot here.
    EXPECT_EQ(static_cast<TracedCondition *>(rule->get_condition(0))->name, "first");
    EXPECT_EQ(static_cast<TracedCondition *>(rule->get_condition(1))->name, "last");

    //Still evaluable through the surviving pointers
    EXPECT_TRUE(rule->CheckConditions());
}

TEST_F(RuleOwnershipTest, RemoveActionDestroysItAndKeepsTheOrderOfTheOthers)
{
    std::unique_ptr<Rule> rule(new Rule("rule", "r"));
    rule->AddAction(new TracedAction("first"));
    rule->AddAction(new TracedAction("middle"));
    rule->AddAction(new TracedAction("last"));

    rule->RemoveAction(1);

    ASSERT_EQ(rule->get_size_actions(), 2);
    EXPECT_EQ(destroyLog, std::vector<std::string>{ "action:middle" });
    EXPECT_EQ(static_cast<TracedAction *>(rule->get_action(0))->name, "first");
    EXPECT_EQ(static_cast<TracedAction *>(rule->get_action(1))->name, "last");
}

//Out of range removals are logged no-ops. The raw pointer version walked the
//iterator past end() and erased whatever was there.
TEST_F(RuleOwnershipTest, RemoveOutOfRangeIsANoOp)
{
    std::unique_ptr<Rule> rule(new Rule("rule", "r"));
    rule->AddCondition(new TracedCondition("c"));
    rule->AddAction(new TracedAction("a"));

    rule->RemoveCondition(1);
    rule->RemoveCondition(-1);
    rule->RemoveAction(7);
    rule->RemoveAction(-3);

    EXPECT_EQ(rule->get_size_conds(), 1);
    EXPECT_EQ(rule->get_size_actions(), 1);
    EXPECT_TRUE(destroyLog.empty());
}

//Removing the same index twice cannot free the same object twice: the first
//call took it out of the vector before destroying it, the second one sees a
//shorter vector.
TEST_F(RuleOwnershipTest, RemovingTheSameConditionTwiceIsNotADoubleFree)
{
    std::unique_ptr<Rule> rule(new Rule("rule", "r"));
    rule->AddCondition(new TracedCondition("only"));

    rule->RemoveCondition(0);
    rule->RemoveCondition(0);

    EXPECT_EQ(rule->get_size_conds(), 0);
    EXPECT_EQ(destroyLog.size(), 1u) << "the condition was destroyed twice";
}

/******************************************************************************
 * ListeRule owns its rules
 ******************************************************************************/

//Remove(int) destroys the rule and, with it, everything the rule owns.
TEST_F(RuleOwnershipTest, RemoveByIndexDestroysTheRuleAndItsContents)
{
    OwnedListeRule list;
    Rule *rule = tracedRule("gone", 1, 1);
    std::weak_ptr<bool> token = rule->aliveToken();
    list.Add(rule);

    ASSERT_EQ(list.size(), 1);
    list.Remove(0);

    EXPECT_EQ(list.size(), 0);
    EXPECT_TRUE(token.expired()) << "the rule outlived its owner's removal";
    EXPECT_EQ(destroyLog.size(), 2u);
}

//Remove(Rule *) does the same, and the removal keeps the ORDER of the rest:
//that order is the rule evaluation order (collectTriggeredRules() and
//ExecuteStartRules() march the list front to back).
TEST_F(RuleOwnershipTest, RemoveKeepsTheOrderOfTheRemainingRules)
{
    OwnedListeRule list;
    for (int i = 0; i < 5; i++)
        list.Add(new Rule("rule", "r" + std::to_string(i)));

    Rule *middle = list.get_rule(2);
    std::weak_ptr<bool> token = middle->aliveToken();

    list.Remove(middle);

    EXPECT_TRUE(token.expired());
    const std::vector<std::string> expected = { "r0", "r1", "r3", "r4" };
    EXPECT_EQ(ruleOrder(list), expected);

    //operator[] and get_rule() agree, and both hand out the live objects
    for (int i = 0; i < list.size(); i++)
        EXPECT_EQ(list[i], list.get_rule(i));
}

//Removing a rule twice must not free it twice. The second call no longer finds
//it in the list.
TEST_F(RuleOwnershipTest, RemovingTheSameRuleTwiceIsNotADoubleFree)
{
    OwnedListeRule list;
    Rule *rule = tracedRule("once", 1, 1);
    list.Add(rule);

    list.Remove(rule);
    list.Remove(rule);      //`rule` is dangling on purpose: only compared, never read
    list.Remove(0);         //out of range now
    list.Remove(static_cast<Rule *>(nullptr));

    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(destroyLog.size(), 2u) << "the rule was destroyed twice";
}

//Remove(Rule *) may only destroy what the list owns. The raw pointer version
//did `delete obj` unconditionally, which destroyed a rule belonging to someone
//else (and was plain UB for a non-heap one). No caller relies on it: every
//`new Rule` of the tree is handed to Add() before any Remove().
TEST_F(RuleOwnershipTest, RemoveOfARuleThisListDoesNotOwnLeavesItAlone)
{
    OwnedListeRule list;
    list.Add(new Rule("rule", "owned"));

    std::unique_ptr<Rule> stranger(tracedRule("stranger", 1, 0));

    list.Remove(stranger.get());

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(destroyLog.empty()) << "a rule owned by someone else was destroyed";
    EXPECT_EQ(stranger->get_name(), "stranger");
}

//Destroying the list destroys every rule it still holds, front to back.
TEST_F(RuleOwnershipTest, DestroyingTheListDestroysEveryRuleItOwns)
{
    std::weak_ptr<bool> first, second;

    {
        OwnedListeRule list;

        Rule *a = tracedRule("a", 1, 0);
        Rule *b = tracedRule("b", 1, 0);
        first = a->aliveToken();
        second = b->aliveToken();
        list.Add(a);
        list.Add(b);

        ASSERT_FALSE(first.expired());
    }

    EXPECT_TRUE(first.expired());
    EXPECT_TRUE(second.expired());

    const std::vector<std::string> expected = { "cond:a.0", "cond:b.0" };
    EXPECT_EQ(destroyLog, expected);
}

//A null rule is ignored instead of being pushed into the list and dereferenced
//by the debug log right below.
TEST_F(RuleOwnershipTest, AddIgnoresANullRule)
{
    OwnedListeRule list;
    list.Add(static_cast<Rule *>(nullptr));
    EXPECT_EQ(list.size(), 0);
}

/******************************************************************************
 * rules_scenarios is a NON-OWNING index over the same rules
 ******************************************************************************/

//A rule carrying "auto_scenario" sits in both containers. Only `rules` owns
//it, so a removal must drop the index entry as well - otherwise
//getRuleAutoScenario() would hand out a dangling pointer, and a second owner
//would double free it.
TEST_F(RuleOwnershipTest, RemovingAnAutoScenarioRuleDropsItFromTheIndex)
{
    OwnedListeRule list;

    Rule *sc = new Rule("AutoScenario", "sc_step");
    sc->set_param("auto_scenario", "sc_1");
    std::weak_ptr<bool> token = sc->aliveToken();
    list.Add(sc);

    Rule *other = new Rule("AutoScenario", "other_step");
    other->set_param("auto_scenario", "sc_2");
    list.Add(other);

    ASSERT_EQ(list.getRuleAutoScenario("sc_1").size(), 1u);
    EXPECT_EQ(list.getRuleAutoScenario("sc_1").front(), sc);

    list.Remove(sc);

    EXPECT_TRUE(token.expired());
    EXPECT_TRUE(list.getRuleAutoScenario("sc_1").empty())
        << "the non-owning index still points at a destroyed rule";
    //The untouched one is still indexed and still alive
    ASSERT_EQ(list.getRuleAutoScenario("sc_2").size(), 1u);
    EXPECT_EQ(list.getRuleAutoScenario("sc_2").front(), other);
}

//Same through Remove(int), which is the path CoreFixture::clearCoreState() and
//the JSON API use.
TEST_F(RuleOwnershipTest, RemoveByIndexDropsTheAutoScenarioIndexEntryToo)
{
    OwnedListeRule list;

    Rule *sc = new Rule("AutoScenario", "sc_step");
    sc->set_param("auto_scenario", "sc_1");
    list.Add(sc);

    ASSERT_EQ(list.getRuleAutoScenario("sc_1").size(), 1u);
    list.Remove(0);
    EXPECT_TRUE(list.getRuleAutoScenario("sc_1").empty());
}

/******************************************************************************
 * The singleton path, end to end
 ******************************************************************************/

//RemoveRule(io) drops every rule referencing the IO, destroys them, and leaves
//the others in their original order.
TEST_F(RuleOwnershipTest, RemoveRuleByIoDestroysTheRulesAndKeepsTheOthersInOrder)
{
    loadConfig(minimalIoXml(), rulesXmlDocument(
                   simpleRuleXml("keep1", ID_INT, "==", "1", ID_BOOL_OUT, "true") +
                   simpleRuleXml("drop", ID_STRING, "==", "x", ID_BOOL_OUT, "true") +
                   simpleRuleXml("keep2", ID_INT, "==", "2", ID_BOOL_OUT, "true")));

    ListeRule &list = ListeRule::Instance();
    ASSERT_EQ(list.size(), 3);

    Rule *dropped = list.get_rule(1);
    ASSERT_EQ(dropped->get_name(), "drop");
    std::weak_ptr<bool> token = dropped->aliveToken();

    ASSERT_TRUE(deleteIO(io(ID_STRING)));

    EXPECT_TRUE(token.expired()) << "the rule using the deleted IO was not destroyed";
    const std::vector<std::string> expected = { "keep1", "keep2" };
    EXPECT_EQ(ruleOrder(list), expected);
}

//What clearCoreState() does: Remove(0) until the list is empty. Every rule is
//destroyed exactly once and the list ends up empty (the raw pointer version
//left the vector full of dangling pointers while it worked).
TEST_F(RuleOwnershipTest, DrainingTheSingletonDestroysEveryRuleExactlyOnce)
{
    ListeRule &list = ListeRule::Instance();

    std::vector<std::weak_ptr<bool>> tokens;
    for (int i = 0; i < 4; i++)
    {
        Rule *rule = tracedRule("drained" + std::to_string(i), 1, 1);
        tokens.push_back(rule->aliveToken());
        list.Add(rule);
    }
    ASSERT_EQ(list.size(), 4);

    while (list.size() > 0)
        list.Remove(0);

    EXPECT_EQ(list.size(), 0);
    for (const std::weak_ptr<bool> &t: tokens)
        EXPECT_TRUE(t.expired());
    EXPECT_EQ(destroyLog.size(), 8u);
}
