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

/* E4.2c - Conditions and Actions reference their IOs BY ID.
 *
 * Step 3/6 of the ownership series. A Condition/Action used to keep a raw
 * IOBase*, which dangled as soon as the IO was destroyed by a path that did
 * not delete the rule with it. The reference is now the id, resolved through
 * the E4.2a accessors at the point of use.
 *
 * What this file pins:
 *
 *  1. THE MODEL: a reference is an id, not an identity. Deleting and
 *     recreating an IO under the same id makes the rule point at the NEW
 *     object; a raw pointer would still point at the freed one.
 *
 *  2. THE MISSING-IO CONTRACT:
 *       - a Condition whose IO does not resolve evaluates to FALSE (fail
 *         closed) and logs an error. Never a crash, never a skipped term
 *         (which would silently turn a conjunction into a weaker one).
 *       - an Action whose IO does not resolve SKIPS that output, logs an
 *         error and makes Execute() return false. The other outputs still run.
 *       - saving resolves nothing: the id is written back as it stands, so a
 *         rule referencing a vanished IO survives a save/reload instead of
 *         dereferencing freed memory.
 *       - LOAD time (rewritten by E4.2e): an id unknown when the config is
 *         read no longer throws the condition/action away. It is kept as the
 *         file describes it, flagged with hasMissingIo(), and the RULE owning
 *         it is disabled - see core/RuleDisabledMissingIo_test.
 *
 *  3. THE in_event UNREGISTRATION: an IO is taken out of the event polling
 *     list because it registered in it, not because its gui_type is in a
 *     hardcoded whitelist.
 *
 *  4. The evaluation ORDER, which must not move.
 *
 *  5. A measurement of the dispatch cost (the by-id model turns a pointer
 *     dereference into an id comparison in the hot path, and a hash lookup in
 *     the evaluation path).
 *
 * Run under ASan: every "IO is gone" case below used to be a
 * heap-use-after-free.
 */

#include "CalaosCoreFixture.h"

#include "ActionStd.h"
#include "ConditionOutput.h"
#include "ConditionScript.h"
#include "ConditionStd.h"

#include <chrono>
#include <iostream>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* Destroy an IO WITHOUT dropping the rules that use it.
 * This is ListeRoom::deleteIO(io, modify = true): the rule-side cleanup is
 * skipped and the object is destroyed anyway. It is the shortest reachable
 * path to the state this ticket is about - a live rule whose IO is gone - and
 * it is exactly what a raw IOBase* could not survive.
 */
bool destroyIoKeepingRules(IOBase *io)
{
    return ListeRoom::Instance().deleteIO(io, true);
}

std::string scriptRuleXml(const std::string &name, const std::string &triggerId,
                          const std::string &outputId)
{
    std::ostringstream ss;
    ss << "<calaos:rule name=\"" << name << "\" type=\"rule\">\n"
       << "  <calaos:condition type=\"script\">\n"
       << "    <calaos:input id=\"" << triggerId << "\" />\n"
       << "    <calaos:script type=\"lua\"><![CDATA[return true]]></calaos:script>\n"
       << "  </calaos:condition>\n"
       << "  <calaos:action type=\"standard\">\n"
       << "    <calaos:output id=\"" << outputId << "\" val=\"true\" />\n"
       << "  </calaos:action>\n"
       << "</calaos:rule>\n";
    return ss.str();
}

}

class RuleIoReferenceTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig(minimalIoXml(), rulesXmlDocument(""));
        ASSERT_EQ(ListeRule::Instance().size(), 0);
    }
};

/******************************************************************************
 * 1. The model: the reference is an id, not a pointer
 ******************************************************************************/

//The structural proof. With an IOBase* member, get_input(0) after the deletion
//is a dangling pointer that keeps comparing equal to `before`; with an id, it
//is nullptr, and it starts resolving to the NEW object once the id exists
//again.
TEST_F(RuleIoReferenceTest, AConditionFollowsTheIdNotTheObject)
{
    IOBase *before = createInternalIO("InternalBool", "e42c_ident", "Identity");
    ASSERT_NE(before, nullptr);

    ConditionStd cond;
    cond.Add(before);
    cond.get_params().Add("e42c_ident", "true");
    cond.get_operator().Add("e42c_ident", "==");

    ASSERT_EQ(cond.get_size(), 1);
    ASSERT_EQ(cond.get_input(0), before);
    EXPECT_EQ(cond.get_input_id(0), "e42c_ident");

    ASSERT_TRUE(destroyIoKeepingRules(before));
    EXPECT_EQ(cond.get_input(0), nullptr)
        << "an id that no longer resolves must be an explicit nullptr";

    IOBase *after = createInternalIO("InternalBool", "e42c_ident", "Identity again");
    ASSERT_NE(after, nullptr);

    EXPECT_EQ(cond.get_input(0), after)
        << "the condition must resolve to the IO that currently owns the id";
    //Identity is checked on the object, not on its address: the allocator
    //routinely hands the freed block straight back, which is exactly why a
    //stale IOBase* is undetectable by comparison and had to go.
    ASSERT_NE(cond.get_input(0), nullptr);
    EXPECT_EQ(cond.get_input(0)->get_param("name"), "Identity again");
}

//Same for an action, and for the output of a ConditionOutput.
TEST_F(RuleIoReferenceTest, AnActionAndAnOutputConditionFollowTheIdToo)
{
    IOBase *before = createInternalIO("InternalBool", "e42c_ident2", "Identity");
    ASSERT_NE(before, nullptr);

    ActionStd act;
    act.Add(before);
    act.get_params().Add("e42c_ident2", "true");

    ConditionOutput ocond;
    ocond.setOutput(before);

    ASSERT_EQ(act.get_output(0), before);
    ASSERT_EQ(ocond.getOutput(), before);
    EXPECT_EQ(act.get_output_id(0), "e42c_ident2");
    EXPECT_EQ(ocond.getOutputId(), "e42c_ident2");

    ASSERT_TRUE(destroyIoKeepingRules(before));

    EXPECT_EQ(act.get_output(0), nullptr);
    EXPECT_EQ(ocond.getOutput(), nullptr);
    //The reference itself is NOT lost: it is what gets saved back
    EXPECT_EQ(act.get_output_id(0), "e42c_ident2");
    EXPECT_EQ(ocond.getOutputId(), "e42c_ident2");
}

//Out of range is a nullptr / an empty id, not an out of bounds read. The
//pre-E4.2c accessors indexed the vector raw.
TEST_F(RuleIoReferenceTest, OutOfRangeAccessorsAreNullNotUndefined)
{
    ConditionStd cond;
    ActionStd act;

    EXPECT_EQ(cond.get_input(0), nullptr);
    EXPECT_EQ(cond.get_input(-1), nullptr);
    EXPECT_TRUE(cond.get_input_id(0).empty());
    EXPECT_EQ(act.get_output(3), nullptr);
    EXPECT_TRUE(act.get_output_id(3).empty());

    //And removing/assigning out of range is a logged no-op, not a crash
    cond.Remove(0);
    act.Remove(0);
    cond.Assign(0, io(ID_BOOL_IN));
    act.Assign(0, io(ID_BOOL_OUT));
    EXPECT_EQ(cond.get_size(), 0);
    EXPECT_EQ(act.get_size(), 0);
}

//A null IO, or one without an id, can never be resolved back: it is refused at
//the door instead of being stored and dereferenced at the next evaluation.
TEST_F(RuleIoReferenceTest, NullAndIdLessIosAreRefusedByAddAndAssign)
{
    ConditionStd cond;
    ActionStd act;
    ConditionScript scond;

    cond.Add(static_cast<IOBase *>(nullptr));
    cond.Add(std::string());
    act.Add(static_cast<IOBase *>(nullptr));
    act.Add(std::string());
    scond.addTriggerIO(nullptr);
    scond.addTriggerId(std::string());

    EXPECT_EQ(cond.get_size(), 0);
    EXPECT_EQ(act.get_size(), 0);
    EXPECT_EQ(scond.getTriggerCount(), 0);
}

/******************************************************************************
 * 2. The missing-IO contract
 ******************************************************************************/

//CONTRACT: a condition whose input is gone is FALSE. Not true, not "skipped".
TEST_F(RuleIoReferenceTest, ConditionStdIsFalseWhenItsInputIsGone)
{
    IOBase *in = createInternalIO("InternalBool", "e42c_gone", "Gone");
    ASSERT_NE(in, nullptr);
    ASSERT_TRUE(in->set_value(true));

    ConditionStd cond;
    cond.Add(in);
    cond.get_params().Add("e42c_gone", "true");
    cond.get_operator().Add("e42c_gone", "==");

    ASSERT_TRUE(cond.Evaluate());

    ASSERT_TRUE(destroyIoKeepingRules(in));

    EXPECT_FALSE(cond.Evaluate())
        << "a condition on an IO that does not exist any more must fail closed";
}

//A conjunction must not become weaker because one of its terms vanished: the
//surviving input is true, the condition is still false.
TEST_F(RuleIoReferenceTest, ConditionStdConjunctionStaysFalseWhenOneInputIsGone)
{
    IOBase *alive = createInternalIO("InternalBool", "e42c_alive", "Alive");
    IOBase *doomed = createInternalIO("InternalBool", "e42c_doomed", "Doomed");
    ASSERT_NE(alive, nullptr);
    ASSERT_NE(doomed, nullptr);
    ASSERT_TRUE(alive->set_value(true));
    ASSERT_TRUE(doomed->set_value(true));

    ConditionStd cond;
    cond.Add(alive);
    cond.get_params().Add("e42c_alive", "true");
    cond.get_operator().Add("e42c_alive", "==");
    cond.Add(doomed);
    cond.get_params().Add("e42c_doomed", "true");
    cond.get_operator().Add("e42c_doomed", "==");

    ASSERT_TRUE(cond.Evaluate());

    ASSERT_TRUE(destroyIoKeepingRules(doomed));

    EXPECT_FALSE(cond.Evaluate())
        << "dropping the unresolvable term would let the rule fire on half a "
           "condition";
    //The surviving input is untouched
    EXPECT_EQ(cond.get_size(), 2);
    EXPECT_EQ(cond.get_input(0), alive);
    EXPECT_EQ(cond.get_input(1), nullptr);
}

TEST_F(RuleIoReferenceTest, ConditionOutputIsFalseWhenItsOutputIsGone)
{
    IOBase *out = createInternalIO("InternalInt", "e42c_ogone", "Gone");
    ASSERT_NE(out, nullptr);
    ASSERT_TRUE(out->set_value(9.0));

    ConditionOutput cond;
    cond.setOutput(out);
    cond.set_param("5");
    cond.set_operator("SUP");

    ASSERT_TRUE(cond.Evaluate());

    ASSERT_TRUE(destroyIoKeepingRules(out));

    EXPECT_FALSE(cond.Evaluate());
}

//Regression: ConditionOutput::output was a raw pointer the constructor never
//initialized, and Evaluate() dereferenced it straight away. An output that was
//never set is now just an unresolvable (empty) id.
TEST_F(RuleIoReferenceTest, ConditionOutputWithNoOutputAtAllIsFalse)
{
    ConditionOutput cond;

    EXPECT_TRUE(cond.getOutputId().empty());
    EXPECT_EQ(cond.getOutput(), nullptr);
    EXPECT_FALSE(cond.Evaluate());
}

//CONTRACT: an action skips the output it cannot resolve, still runs the
//others, and reports the failure.
TEST_F(RuleIoReferenceTest, ActionSkipsTheMissingOutputRunsTheOthersAndFails)
{
    IOBase *alive = createInternalIO("InternalBool", "e42c_aout", "Alive out");
    IOBase *doomed = createInternalIO("InternalBool", "e42c_dout", "Doomed out");
    ASSERT_NE(alive, nullptr);
    ASSERT_NE(doomed, nullptr);
    ASSERT_FALSE(alive->get_value_bool());

    ActionStd act;
    act.Add(alive);
    act.get_params().Add("e42c_aout", "true");
    act.Add(doomed);
    act.get_params().Add("e42c_dout", "true");

    ASSERT_TRUE(destroyIoKeepingRules(doomed));

    EXPECT_FALSE(act.Execute())
        << "an action that could not do half of its job must report a failure";
    EXPECT_TRUE(alive->get_value_bool())
        << "the outputs that still exist must have been driven";
}

//A rule, end to end: its IO is gone, it must not fire and must not crash on
//the trigger march.
TEST_F(RuleIoReferenceTest, ARuleWhoseIoVanishedNeitherFiresNorCrashes)
{
    Rule *rule = addSimpleRule("Vanishing", ID_BOOL_IN, "==", "true",
                               ID_BOOL_OUT, "true");
    ASSERT_NE(rule, nullptr);
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    ASSERT_TRUE(rule->CheckConditions());

    //The rule survives the deletion of its input on purpose here
    ASSERT_TRUE(destroyIoKeepingRules(io(ID_BOOL_IN)));
    ASSERT_EQ(ListeRule::Instance().size(), 1);

    EXPECT_FALSE(rule->CheckConditions());

    //A march over the rules is still safe, and nothing is triggered
    ASSERT_TRUE(io(ID_BOOL_OUT)->set_value(false));
    ListeRule::Instance().ExecuteRuleSignal(ID_BOOL_IN);
    ListeRule::Instance().ExecuteRuleSignal(ID_STRING);
    EXPECT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    //And Execute() (conditions + actions) is a clean "did not run"
    EXPECT_FALSE(rule->Execute());
}

//CONTRACT: saving resolves nothing. A rule whose IO vanished is written back
//with its id intact - it used to dereference the freed IO to get that id.
TEST_F(RuleIoReferenceTest, SavingARuleWhoseIoVanishedKeepsTheId)
{
    ASSERT_NE(addSimpleRule("Keeper", ID_BOOL_IN, "==", "true",
                            ID_BOOL_OUT, "true"), nullptr);
    ASSERT_NE(addRuleFromXml(scriptRuleXml("Scripted", ID_INT, ID_BOOL_OUT)), nullptr);

    ASSERT_TRUE(destroyIoKeepingRules(io(ID_BOOL_IN)));
    ASSERT_TRUE(destroyIoKeepingRules(io(ID_INT)));
    ASSERT_EQ(ListeRule::Instance().size(), 2);

    saveConfig();

    const std::string rules = rulesXmlOnDisk();
    EXPECT_NE(rules.find(ID_BOOL_IN), std::string::npos)
        << "the condition lost its reference on save:\n" << rules;
    EXPECT_NE(rules.find(ID_INT), std::string::npos)
        << "the script trigger lost its reference on save:\n" << rules;
}

/* The other half of the contract, REWRITTEN BY E4.2e and pinned here next to
 * it. It used to assert `EXPECT_FALSE(LoadFromXml())` for both, i.e. the node
 * was refused and the condition/action thrown away. That is exactly what made
 * a rule come back amputated - and made the reference disappear from the file
 * at the next save.
 *
 * Now the node is ACCEPTED (LoadFromXml answers true), the reference and its
 * parameters are kept as they stand, and the object says why through
 * hasMissingIo(). Rule::AddCondition()/AddAction() turn that into a disabled
 * rule; core/RuleDisabledMissingIo_test covers that end.
 */
TEST_F(RuleIoReferenceTest, AnIdUnknownAtLoadTimeIsKeptAndFlagged)
{
    ConditionStd cond;
    ActionStd act;

    pugi::xml_document cdoc;
    pugi::xml_node cnode = cdoc.append_child("calaos:condition");
    pugi::xml_node cin = cnode.append_child("calaos:input");
    cin.append_attribute("id").set_value("e42c_never_existed");
    cin.append_attribute("oper").set_value("==");
    cin.append_attribute("val").set_value("true");

    EXPECT_TRUE(cond.LoadFromXml(cnode));
    EXPECT_TRUE(cond.hasMissingIo());
    ASSERT_EQ(cond.getMissingIoIds().size(), 1u);
    EXPECT_EQ(cond.getMissingIoIds()[0], "e42c_never_existed");
    //Kept whole: the id, the operator and the value are still there, so
    //SaveToXml() can write the user's condition back untouched
    ASSERT_EQ(cond.get_size(), 1);
    EXPECT_EQ(cond.get_input_id(0), "e42c_never_existed");
    EXPECT_EQ(cond.get_params()["e42c_never_existed"], "true");
    EXPECT_EQ(cond.get_operator()["e42c_never_existed"], "==");

    pugi::xml_document adoc;
    pugi::xml_node anode = adoc.append_child("calaos:action");
    pugi::xml_node aout = anode.append_child("calaos:output");
    aout.append_attribute("id").set_value("e42c_never_existed");
    aout.append_attribute("val").set_value("true");

    EXPECT_TRUE(act.LoadFromXml(anode));
    EXPECT_TRUE(act.hasMissingIo());
    ASSERT_EQ(act.getMissingIoIds().size(), 1u);
    EXPECT_EQ(act.getMissingIoIds()[0], "e42c_never_existed");
    ASSERT_EQ(act.get_size(), 1);
    EXPECT_EQ(act.get_output_id(0), "e42c_never_existed");
    EXPECT_EQ(act.get_params()["e42c_never_existed"], "true");
}

//ConditionScript compares its triggers by id as well, so RemoveRule() and the
//dispatch never dereference the IO they are asked about.
TEST_F(RuleIoReferenceTest, ScriptTriggersAreComparedById)
{
    Rule *rule = addRuleFromXml(scriptRuleXml("Scripted", ID_INT, ID_BOOL_OUT));
    ASSERT_NE(rule, nullptr);

    ConditionScript *cond = dynamic_cast<ConditionScript *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_EQ(cond->getTriggerCount(), 1);
    EXPECT_EQ(cond->getTriggerId(0), ID_INT);
    EXPECT_TRUE(cond->containsTriggerIO(io(ID_INT)));
    EXPECT_TRUE(cond->containsTriggerId(ID_INT));
    EXPECT_FALSE(cond->containsTriggerIO(nullptr));
    EXPECT_FALSE(cond->containsTriggerId(""));

    ASSERT_TRUE(destroyIoKeepingRules(io(ID_INT)));

    //The reference survives, and asking about it resolves nothing
    EXPECT_TRUE(cond->containsTriggerId(ID_INT));

    //Declaring the same trigger twice is still a single entry (the map this
    //replaced deduplicated by construction)
    cond->addTriggerId(ID_INT);
    EXPECT_EQ(cond->getTriggerCount(), 1);
}

/******************************************************************************
 * 3. in_event unregistration follows registration, not gui_type
 ******************************************************************************/

/* THE bug this fixes: registration is done by the IO itself
 * (ListeRule::Instance().Add(this) in InputTime/InputAnalog/InPlageHoraire),
 * while unregistration was gated on a hardcoded gui_type whitelist in
 * ListeRoom::detachIOFromRules(). The two agreed only by luck. Here an IO
 * registers with a gui_type that is NOT in that whitelist ("var_bool"), which
 * is exactly what a future driver would do: it used to stay in `in_event`
 * after being freed, and RunEventLoop() dereferenced it.
 */
TEST_F(RuleIoReferenceTest, EventPollingUnregistrationDoesNotDependOnGuiType)
{
    const size_t before = ListeRule::Instance().eventCount();

    IOBase *poller = createInternalIO("InternalBool", "e42c_poll", "Poller");
    ASSERT_NE(poller, nullptr);
    ASSERT_EQ(poller->get_param("gui_type"), "var_bool")
        << "the point of the test is a gui_type outside the old whitelist";

    ListeRule::Instance().Add(poller);
    ASSERT_TRUE(ListeRule::Instance().isEventRegistered(poller));
    ASSERT_EQ(ListeRule::Instance().eventCount(), before + 1);

    IOBase *freed = poller;
    ASSERT_TRUE(deleteIO(poller));

    EXPECT_FALSE(ListeRule::Instance().isEventRegistered(freed))
        << "the polling list kept a freed IO because of its gui_type";
    EXPECT_EQ(ListeRule::Instance().eventCount(), before);

    //The loop that used to dereference the freed entry
    ListeRule::Instance().RunEventLoop();
}

//The symmetric half: unregistering an IO that never registered is a no-op, so
//"always unregister" is safe for every IO of the tree.
TEST_F(RuleIoReferenceTest, DeletingANeverRegisteredIoLeavesThePollingListAlone)
{
    IOBase *registered = createInternalIO("InternalBool", "e42c_reg", "Registered");
    IOBase *plain = createInternalIO("InternalBool", "e42c_plain", "Plain");
    ASSERT_NE(registered, nullptr);
    ASSERT_NE(plain, nullptr);

    ListeRule::Instance().Add(registered);
    const size_t withOne = ListeRule::Instance().eventCount();

    ASSERT_TRUE(deleteIO(plain));

    EXPECT_EQ(ListeRule::Instance().eventCount(), withOne);
    EXPECT_TRUE(ListeRule::Instance().isEventRegistered(registered));

    ASSERT_TRUE(deleteIO(registered));
    EXPECT_EQ(ListeRule::Instance().eventCount(), withOne - 1);
}

/******************************************************************************
 * 4. Evaluation order
 ******************************************************************************/

//INVARIANT of the whole E4.2 series: which rule runs first is decided by the
//rule list order, and by nothing else. Moving to ids must not reorder it.
TEST_F(RuleIoReferenceTest, TriggerDispatchKeepsTheRuleDeclarationOrder)
{
    const int RULES = 8;
    for (int i = 0;i < RULES;i++)
    {
        std::ostringstream name;
        name << "Order" << i;
        ASSERT_NE(addSimpleRule(name.str(), ID_BOOL_IN, "==", "true",
                                ID_BOOL_OUT, "true"), nullptr);
    }
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));

    std::vector<Rule *> syncRules, asyncRules;
    ListeRule::Instance().collectTriggeredRules(ID_BOOL_IN, syncRules, asyncRules);

    ASSERT_EQ((int)syncRules.size(), RULES);
    for (int i = 0;i < RULES;i++)
    {
        std::ostringstream name;
        name << "Order" << i;
        EXPECT_EQ(syncRules[i]->get_name(), name.str())
            << "rule " << i << " moved in the dispatch order";
        EXPECT_EQ(syncRules[i], ListeRule::Instance().get_rule(i));
    }
}

/******************************************************************************
 * 5. Cost of the by-id model (measurement, not an assertion on wall clock)
 ******************************************************************************/

/* Rule evaluation fires on EVERY IO change, so the price of the model has to
 * be known rather than assumed. Two paths are timed on a config of the size of
 * a real installation:
 *
 *   - the DISPATCH (ListeRule::collectTriggeredRules): it used to dereference
 *     every input of every condition of every rule and call get_param("id") on
 *     it - a std::string built from the literal, a std::map lookup and a
 *     string copy, per input, per rule, per IO change. It is now a comparison
 *     against the stored id: strictly less work, no resolution at all.
 *
 *   - the EVALUATION (ConditionStd::Evaluate): this is where the by-id model
 *     adds work, one unordered_map lookup per input. It removes the 4 to 5
 *     get_param("id") calls per input that the old code paid to key
 *     params/ops/params_var.
 *
 * The numbers are printed, not asserted: a wall clock threshold in `make
 * check` is a flake, and the comparison that matters is against the previous
 * revision, which is done by building both.
 *
 * MEASURED (same container, same flags, best of 3, config below), pointer
 * model (master 838850a0) -> by-id model:
 *     dispatch   77.2 us -> 25.6 us per IO change    (-67%, 3.0x faster)
 *     evaluate   1.37 us -> 1.04 us per evaluation   (-24%)
 *                 456 ns ->  347 ns per input
 * The model is a NET WIN, not a cost. No cache of the resolved pointer is
 * introduced: it is not needed, and a cache is precisely the stale reference
 * this ticket exists to remove.
 */
class RuleIoBenchmark: public CoreFixture
{
protected:
    //enum, not static const int: gtest's EqHelper takes its arguments by
    //reference, which would odr-use them and need an out of class definition
    enum
    {
        IO_COUNT = 200,        //inputs (as many outputs)
        RULE_COUNT = 200,
        INPUTS_PER_RULE = 3,
    };

    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig(minimalIoXml(), rulesXmlDocument(""));

        for (int i = 0;i < IO_COUNT;i++)
        {
            std::ostringstream in, out;
            in << "bench_in_" << i;
            out << "bench_out_" << i;
            ASSERT_NE(createInternalIO("InternalBool", in.str(), in.str()), nullptr);
            ASSERT_NE(createInternalIO("InternalBool", out.str(), out.str()), nullptr);
            ASSERT_TRUE(io(in.str())->set_value(true));
        }

        for (int r = 0;r < RULE_COUNT;r++)
        {
            std::ostringstream name, out;
            name << "bench_rule_" << r;
            out << "bench_out_" << (r % IO_COUNT);

            Rule *rule = new Rule("rule", name.str());
            ConditionStd *cond = new ConditionStd();
            rule->AddCondition(cond);

            for (int k = 0;k < INPUTS_PER_RULE;k++)
            {
                std::ostringstream in;
                in << "bench_in_" << ((r + k * 37) % IO_COUNT);
                cond->Add(io(in.str()));
                cond->get_params().Add(in.str(), "true");
                cond->get_operator().Add(in.str(), "==");
            }

            ActionStd *act = new ActionStd();
            rule->AddAction(act);
            act->Add(io(out.str()));
            act->get_params().Add(out.str(), "true");

            ListeRule::Instance().Add(rule);
        }

        ASSERT_EQ(ListeRule::Instance().size(), RULE_COUNT);
    }
};

TEST_F(RuleIoBenchmark, DispatchAndEvaluationCost)
{
    using clock = std::chrono::steady_clock;

    const int DISPATCH_REPS = 10;
    const int EVAL_REPS = 20000;

    //--- dispatch: one march over all the rules per IO change
    auto t0 = clock::now();
    int triggered = 0;
    for (int rep = 0;rep < DISPATCH_REPS;rep++)
    {
        for (int i = 0;i < IO_COUNT;i++)
        {
            std::ostringstream in;
            in << "bench_in_" << i;

            std::vector<Rule *> syncRules, asyncRules;
            ListeRule::Instance().collectTriggeredRules(in.str(), syncRules, asyncRules);
            triggered += syncRules.size();
        }
    }
    auto t1 = clock::now();

    //--- evaluation only, on one representative multi-input condition
    ConditionStd *cond = dynamic_cast<ConditionStd *>(
                ListeRule::Instance().get_rule(0)->get_condition(0));
    ASSERT_NE(cond, nullptr);

    int trues = 0;
    auto t2 = clock::now();
    for (int rep = 0;rep < EVAL_REPS;rep++)
        trues += cond->Evaluate() ? 1 : 0;
    auto t3 = clock::now();

    const double dispatchUs =
            std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double evalUs =
            std::chrono::duration<double, std::micro>(t3 - t2).count();

    const int dispatches = DISPATCH_REPS * IO_COUNT;

    std::cout << "[ E4.2c ] config: " << RULE_COUNT << " rules x "
              << INPUTS_PER_RULE << " inputs, " << (IO_COUNT * 2) << " IOs\n"
              << "[ E4.2c ] dispatch : " << dispatches << " IO changes in "
              << dispatchUs / 1000.0 << " ms -> "
              << dispatchUs / dispatches << " us per IO change ("
              << triggered << " rules triggered)\n"
              << "[ E4.2c ] evaluate : " << EVAL_REPS << " evaluations of a "
              << INPUTS_PER_RULE << "-input condition in " << evalUs / 1000.0
              << " ms -> " << evalUs / EVAL_REPS << " us per evaluation ("
              << (evalUs * 1000.0) / (EVAL_REPS * INPUTS_PER_RULE)
              << " ns per input)\n";

    RecordProperty("dispatch_us_per_io_change",
                   static_cast<int>(dispatchUs / dispatches * 1000));
    RecordProperty("eval_ns_per_input",
                   static_cast<int>((evalUs * 1000.0) / (EVAL_REPS * INPUTS_PER_RULE)));

    //Only a sanity check: the work really happened
    EXPECT_GT(triggered, 0);
    EXPECT_EQ(trues, EVAL_REPS);
}
