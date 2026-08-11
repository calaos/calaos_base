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

//ConditionStd / ConditionOutput semantics tests (ticket T1.2).
//
//M1: ConditionStd::Evaluate() assigned "ret" on every input of the loop
//instead of accumulating a conjunction, so only the last <calaos:input> ever
//decided the result of a multi-input condition. Fixed to AND all inputs.
//
//M7: the three eval(bool/double/string) overloads were duplicated between
//ConditionStd and ConditionOutput, and ConditionOutput left its "bval"/"dval"
//temporaries uninitialized where ConditionStd already zero-initialized them.
//Both are now backed by one shared operator-evaluation helper, and behavior
//must be identical between the two classes for the same operator/type.

#include "CalaosCoreFixture.h"

#include "ConditionOutput.h"
#include "ConditionStd.h"

using namespace Calaos;
using namespace CalaosTest;

class ConditionEvalTest: public CoreFixture
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
 * M1: multi-input ConditionStd is a conjunction (AND) of every input
 ******************************************************************************/

//The red-before-green test: before the fix, ConditionStd::Evaluate() kept
//overwriting "ret" on every loop iteration, so only the last input's eval()
//result survived. Here the first input evaluates to false and the second to
//true; the pre-fix code returned true (last input wins), the fix must AND
//them together and return false.
TEST_F(ConditionEvalTest, MultiInputConditionIsAConjunctionNotTheLastInput)
{
    IOBase *in1 = io(ID_BOOL_IN);
    IOBase *in2 = createInternalIO("InternalBool", "cond_bool2", "Second bool");
    ASSERT_NE(in1, nullptr);
    ASSERT_NE(in2, nullptr);

    ASSERT_TRUE(in1->set_value(false));
    ASSERT_TRUE(in2->set_value(true));

    ConditionStd cond;
    //First input: actual value is false, condition wants "true" -> eval() is false
    cond.Add(in1);
    cond.get_params().Add(ID_BOOL_IN, "true");
    cond.get_operator().Add(ID_BOOL_IN, "==");
    //Second input: actual value is true, condition wants "true" -> eval() is true
    cond.Add(in2);
    cond.get_params().Add("cond_bool2", "true");
    cond.get_operator().Add("cond_bool2", "==");

    EXPECT_FALSE(cond.Evaluate())
        << "first input is false, second is true: the conjunction of both must be false, "
           "not just the last input's result";
}

TEST_F(ConditionEvalTest, MultiInputConditionIsTrueWhenEveryInputMatches)
{
    IOBase *in1 = io(ID_BOOL_IN);
    IOBase *in2 = createInternalIO("InternalBool", "cond_bool2", "Second bool");
    ASSERT_NE(in1, nullptr);
    ASSERT_NE(in2, nullptr);

    ASSERT_TRUE(in1->set_value(true));
    ASSERT_TRUE(in2->set_value(true));

    ConditionStd cond;
    cond.Add(in1);
    cond.get_params().Add(ID_BOOL_IN, "true");
    cond.get_operator().Add(ID_BOOL_IN, "==");
    cond.Add(in2);
    cond.get_params().Add("cond_bool2", "true");
    cond.get_operator().Add("cond_bool2", "==");

    EXPECT_TRUE(cond.Evaluate());
}

//Same conjunction, exercised through the real XML loading path (LoadFromXml
//already supported N <calaos:input> siblings, only Evaluate() was broken).
TEST_F(ConditionEvalTest, MultiInputConditionViaXmlIsAConjunction)
{
    IOBase *in2 = createInternalIO("InternalBool", "cond_bool2", "Second bool");
    ASSERT_NE(in2, nullptr);
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(false));
    ASSERT_TRUE(in2->set_value(true));

    std::ostringstream ss;
    ss << "<calaos:rule name=\"MultiIn\" type=\"rule\">\n"
       << "  <calaos:condition type=\"standard\" trigger=\"true\">\n"
       << "    <calaos:input id=\"" << ID_BOOL_IN << "\" oper=\"==\" val=\"true\" />\n"
       << "    <calaos:input id=\"cond_bool2\" oper=\"==\" val=\"true\" />\n"
       << "  </calaos:condition>\n"
       << "  <calaos:action type=\"standard\">\n"
       << "    <calaos:output id=\"" << ID_BOOL_OUT << "\" val=\"true\" />\n"
       << "  </calaos:action>\n"
       << "</calaos:rule>\n";

    Rule *rule = addRuleFromXml(ss.str());
    ASSERT_NE(rule, nullptr);
    ASSERT_EQ(rule->get_size_conds(), 1);

    ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(0));
    ASSERT_NE(cond, nullptr);
    ASSERT_EQ(cond->get_size(), 2);

    EXPECT_FALSE(rule->CheckConditions());

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(rule->CheckConditions());
}

/******************************************************************************
 * Single-input non-regression on the 3 value types
 ******************************************************************************/

TEST_F(ConditionEvalTest, SingleBoolInputNonRegression)
{
    ConditionStd cond;
    cond.Add(io(ID_BOOL_IN));
    cond.get_params().Add(ID_BOOL_IN, "true");
    cond.get_operator().Add(ID_BOOL_IN, "==");

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(false));
    EXPECT_FALSE(cond.Evaluate());

    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    EXPECT_TRUE(cond.Evaluate());
}

TEST_F(ConditionEvalTest, SingleIntInputNonRegression)
{
    ConditionStd cond;
    cond.Add(io(ID_INT));
    cond.get_params().Add(ID_INT, "5");
    cond.get_operator().Add(ID_INT, "SUP");

    ASSERT_TRUE(io(ID_INT)->set_value(3.0));
    EXPECT_FALSE(cond.Evaluate());

    ASSERT_TRUE(io(ID_INT)->set_value(9.0));
    EXPECT_TRUE(cond.Evaluate());
}

TEST_F(ConditionEvalTest, SingleStringInputNonRegression)
{
    ConditionStd cond;
    cond.Add(io(ID_STRING));
    cond.get_params().Add(ID_STRING, "hello");
    cond.get_operator().Add(ID_STRING, "==");

    ASSERT_TRUE(io(ID_STRING)->set_value(std::string("world")));
    EXPECT_FALSE(cond.Evaluate());

    ASSERT_TRUE(io(ID_STRING)->set_value(std::string("hello")));
    EXPECT_TRUE(cond.Evaluate());
}

/******************************************************************************
 * M7: ConditionStd / ConditionOutput parity on the 3 types and their operators
 ******************************************************************************/

class ConditionParityTest: public ConditionEvalTest
{
protected:
    //Builds a single-input ConditionStd and an equivalent ConditionOutput on
    //the same IO/operator/value and checks they always agree.
    void expectParity(IOBase *target, const std::string &oper, const std::string &val)
    {
        ConditionStd stdCond;
        stdCond.Add(target);
        stdCond.get_params().Add(target->get_param("id"), val);
        stdCond.get_operator().Add(target->get_param("id"), oper);

        ConditionOutput outCond;
        outCond.setOutput(target);
        outCond.set_param(val);
        outCond.set_operator(oper);

        EXPECT_EQ(stdCond.Evaluate(), outCond.Evaluate())
            << "ConditionStd and ConditionOutput disagree for oper=" << oper
            << " val=" << val << " on IO " << target->get_param("id");
    }
};

TEST_F(ConditionParityTest, BoolOperatorsAgree)
{
    IOBase *b = io(ID_BOOL_IN);

    for (bool actual: { false, true })
    {
        ASSERT_TRUE(b->set_value(actual));
        expectParity(b, "==", "true");
        expectParity(b, "==", "false");
        expectParity(b, "!=", "true");
        expectParity(b, "!=", "false");
    }
}

TEST_F(ConditionParityTest, IntOperatorsAgree)
{
    IOBase *n = io(ID_INT);

    for (double actual: { 1.0, 5.0, 9.0 })
    {
        ASSERT_TRUE(n->set_value(actual));
        expectParity(n, "==", "5");
        expectParity(n, "!=", "5");
        expectParity(n, "SUP", "5");
        expectParity(n, "SUP=", "5");
        expectParity(n, "INF", "5");
        expectParity(n, "INF=", "5");
    }
}

//Unlike the bool/int cases, ConditionOutput's TSTRING branch never calls the
//shared string eval() helper: it routes through IOBase::check_condition_value()
//(ConditionOutput.cpp's fourth, IOBase-based eval() overload, out of scope for
//the M7 extraction). IOBase's default check_condition_value() always returns
//false, so a plain Internal string IO -- which does not override it -- makes
//ConditionOutput reject every string condition, matching or not. This is a
//pre-existing asymmetry with ConditionStd (which does a direct string
//comparison), not something introduced or fixed by this ticket; documented
//here instead of asserted as parity.
TEST_F(ConditionParityTest, ConditionOutputStringGoesThroughCheckConditionValueNotDirectComparison)
{
    IOBase *s = io(ID_STRING);
    ASSERT_TRUE(s->set_value(std::string("hello")));

    ConditionStd stdCond;
    stdCond.Add(s);
    stdCond.get_params().Add(ID_STRING, "hello");
    stdCond.get_operator().Add(ID_STRING, "==");
    EXPECT_TRUE(stdCond.Evaluate());

    ConditionOutput outCond;
    outCond.setOutput(s);
    outCond.set_param("hello");
    outCond.set_operator("==");
    EXPECT_FALSE(outCond.Evaluate())
        << "a plain Internal IO does not override check_condition_value(), "
           "so ConditionOutput's default is to reject every string condition";
}

//ConditionOutput used to leave "bval"/"dval" uninitialized; this exercises the
//"changed" and unresolved-var_id paths that skip assigning them before a
//possible read, on both classes, with the same operator/value pair.
TEST_F(ConditionParityTest, ChangedKeywordAgreesOnBoolAndInt)
{
    ASSERT_TRUE(io(ID_BOOL_IN)->set_value(true));
    expectParity(io(ID_BOOL_IN), "==", "changed");

    ASSERT_TRUE(io(ID_INT)->set_value(4.0));
    expectParity(io(ID_INT), "==", "changed");
}
