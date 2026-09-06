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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * T3.25a §2 - the sampling period of an analog input.
 *
 * InputAnalog::readConfig() decides how often the hardware is read. Three
 * parameters feed it and `double frequency` has no in-class initialiser, so
 * T3.25's rule ("from_string() always writes its destination") turned an
 * indeterminate period into a period of ZERO. Nothing was fixed by that, the
 * defect only became reproducible:
 *
 *   - Exists() proves the key is there, never that it carries a number. A
 *     period= or an interval= that is PRESENT AND EMPTY therefore reads as 0;
 *   - hasChanged() compares `sec >= frequency`, which with frequency == 0 is
 *     always true, so readValue() runs on EVERY turn of the rules loop. On a
 *     Wago, a 1-Wire or a Web input that is a permanent poll of the hardware -
 *     not a wrong value, a load;
 *   - and the legacy branch is worse than the other two, because it WRITES:
 *     it reads "frequency" into that same member and immediately serialises
 *     it into "period", so an unreadable legacy value is SAVED INTO io.xml.
 *     A defect that survives a restart is a different thing from one that
 *     does not.
 *
 * THE ORACLE IS THE POLL, NOT THE MEMBER. Every case asserts how many times
 * readValue() was really called across a run of the rules loop, because that
 * is what the owner of the installation pays for. The period the IO settled
 * on is asserted as well, but never alone: a probe reading a protected member
 * would stay green if hasChanged() itself were exchanged.
 *
 * ⚠️ WHAT IS NOT DECIDED HERE. An explicitly written period="0" still means
 * "read on every turn". That is a value its owner chose, it is inside the
 * documented meaning of the parameter, and refusing it would be a product
 * decision rather than the closing of a hole. Only values that do not READ
 * fall back to the documented 15 s.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>

#include "CalaosCoreFixture.h"
#include "InputAnalog.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* The documented default: "If this value is not set, the default value is
 * 15s" (the "interval" parameter of the IO documentation). */
const double kDocumentedDefaultSec = 15.0;

class AnalogProbe: public InputAnalog
{
public:
    explicit AnalogProbe(Params &p): InputAnalog(p) {}

    int readValueCalls = 0;

    //The period the IO settled on, in seconds.
    double pollPeriodSec() const { return frequency; }

protected:
    void readValue() override { readValueCalls++; }
};

void registerProbe()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T325aAnalog", [](Params &p) -> IOBase * { return new AnalogProbe(p); });
}

/* Turns of the rules loop, enough that a period of zero is unmistakable and
 * few enough that the case costs nothing. */
const int kLoopTurns = 20;

}

class InputAnalogPeriodTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        registerProbe();
        CoreFixture::SetUp();
        loadConfig();
        probe = nullptr;
    }

    void TearDown() override
    {
        //InputAnalog registers itself in ListeRule's non-owning in_event list
        //and nothing takes it back out; clearCoreState() does not touch that
        //list either. Leaving the probe there would hand the next case of this
        //binary a dangling pointer.
        if (probe)
        {
            ListeRule::Instance().Remove(probe);
            delete probe;
            probe = nullptr;
        }
        CoreFixture::TearDown();
    }

    std::string caseName() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }
    std::string ioId() const { return "t325a_analog_" + caseName(); }

    //Builds the IO through IOFactory with the parameters a configuration
    //would carry - readConfig() runs inside the constructor, exactly as it
    //does when io.xml is loaded.
    AnalogProbe *makeProbe(const Params &extra)
    {
        Params p = {{ "type", "T325aAnalog" },
                    { "id", ioId() },
                    { "name", ioId() },
                    { "enabled", "true" },
                    { "visible", "true" }};
        for (Params::const_iterator it = extra.cbegin(); it != extra.cend(); ++it)
            p.Add(it->first, it->second);

        probe = dynamic_cast<AnalogProbe *>(
                    IOFactory::Instance().CreateIO("T325aAnalog", p));
        return probe;
    }

    //Turns of the rules loop. ListeRule::RunEventLoop() calls hasChanged() on
    //every IO that registered itself; this is that call, nothing else.
    int pollsOver(int turns)
    {
        const int before = probe->readValueCalls;
        for (int i = 0; i < turns; i++)
            probe->hasChanged();
        return probe->readValueCalls - before;
    }

    AnalogProbe *probe = nullptr;
};

/*******************************************************************************
 * A parameter that is present and does not read
 ******************************************************************************/

TEST_F(InputAnalogPeriodTest, AnEmptyPeriodKeepsTheDocumentedDefault)
{
    //RED BEFORE THE FIX: Exists("period") is true, from_string("") writes 0,
    //the division keeps it 0, and the input is read on every turn.
    ASSERT_NE(nullptr, makeProbe({{ "period", "" }}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns))
            << "the hardware was polled on every turn of the rules loop";
}

TEST_F(InputAnalogPeriodTest, AnEmptyIntervalKeepsTheDocumentedDefault)
{
    //RED BEFORE THE FIX. The legacy seconds-based parameter takes priority
    //over "period", so it needs its own case: a guard written on the period
    //branch alone would leave this one open.
    ASSERT_NE(nullptr, makeProbe({{ "interval", "" }}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

TEST_F(InputAnalogPeriodTest, AnUnreadablePeriodKeepsTheDocumentedDefault)
{
    //RED BEFORE THE FIX. Same hole, reached with text instead of emptiness.
    ASSERT_NE(nullptr, makeProbe({{ "period", "abc" }}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

TEST_F(InputAnalogPeriodTest, APeriodThatOnlyPartlyReadsIsNotHalfHonoured)
{
    //RED BEFORE THE FIX, and this is the case that WRITES DOWN the policy the
    //fiche asked for. from_string() leaves the digits it managed to read in
    //the destination, so "200abc" used to become a 200 ms period nobody
    //configured. A period is honoured only when the WHOLE parameter reads as
    //a number.
    ASSERT_NE(nullptr, makeProbe({{ "period", "200abc" }}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

TEST_F(InputAnalogPeriodTest, APeriodOutOfRangeKeepsTheDocumentedDefault)
{
    //RED BEFORE THE FIX. from_string() publishes the SATURATED value on an
    //overflow, so this used to settle on a period no clock will ever reach -
    //the mirror image of the empty string, and just as much a value nobody
    //chose. The poll count cannot tell the two apart, the period can.
    ASSERT_NE(nullptr, makeProbe({{ "period", "1e400" }}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

/*******************************************************************************
 * The legacy rename, which persists what it decides
 ******************************************************************************/

TEST_F(InputAnalogPeriodTest, AnUnreadableLegacyFrequencyIsNotWrittenBack)
{
    //RED BEFORE THE FIX, twice. The rename branch reads "frequency" into the
    //member and immediately serialises the member into "period": an
    //unreadable value used to be SAVED as period="0", so the permanent poll
    //survived the restart and outlived the parameter that caused it.
    ASSERT_NE(nullptr, makeProbe({{ "frequency", "abc" }}));

    EXPECT_FALSE(probe->param_exists("frequency"))
            << "the legacy parameter must still be renamed away";
    EXPECT_EQ("15000", probe->get_param("period"))
            << "an unreadable legacy value was serialised into the "
               "configuration";
    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

TEST_F(InputAnalogPeriodTest, AReadableLegacyFrequencyIsStillRenamed)
{
    //GREEN BEFORE AND AFTER. The control for the branch above: the migration
    //that works must keep working, value for value.
    ASSERT_NE(nullptr, makeProbe({{ "frequency", "3000" }}));

    EXPECT_FALSE(probe->param_exists("frequency"));
    EXPECT_EQ("3000", probe->get_param("period"));
    EXPECT_DOUBLE_EQ(3.0, probe->pollPeriodSec());
}

/*******************************************************************************
 * What the default must not cost
 ******************************************************************************/

TEST_F(InputAnalogPeriodTest, AReadablePeriodIsStillHonoured)
{
    //GREEN BEFORE AND AFTER. The period is in milliseconds and stays so.
    ASSERT_NE(nullptr, makeProbe({{ "period", "200" }}));

    EXPECT_DOUBLE_EQ(0.2, probe->pollPeriodSec());
}

TEST_F(InputAnalogPeriodTest, AReadableIntervalIsStillHonouredAndStaysInSeconds)
{
    //GREEN BEFORE AND AFTER. "interval" is seconds, "period" is milliseconds,
    //and confusing the two would divide every legacy installation's sampling
    //rate by a thousand. 7 is not 7000/1000 by accident: it is written here so
    //that swapping the two branches cannot be silent.
    ASSERT_NE(nullptr, makeProbe({{ "interval", "7" }}));

    EXPECT_DOUBLE_EQ(7.0, probe->pollPeriodSec());
}

TEST_F(InputAnalogPeriodTest, IntervalStillWinsOverPeriod)
{
    //GREEN BEFORE AND AFTER. The priority between the two is behaviour an
    //installation depends on; the fix rewrites both branches, so it is pinned.
    ASSERT_NE(nullptr, makeProbe({{ "interval", "7" }, { "period", "200" }}));

    EXPECT_DOUBLE_EQ(7.0, probe->pollPeriodSec());
}

TEST_F(InputAnalogPeriodTest, NoSamplingParameterAtAllIsTheDocumentedDefault)
{
    //GREEN BEFORE AND AFTER. The branch that was already correct.
    ASSERT_NE(nullptr, makeProbe({}));

    EXPECT_DOUBLE_EQ(kDocumentedDefaultSec, probe->pollPeriodSec());
    EXPECT_EQ(0, pollsOver(kLoopTurns));
}

TEST_F(InputAnalogPeriodTest, AnExplicitZeroPeriodStillPollsOnEveryTurn)
{
    //GREEN BEFORE AND AFTER, and it carries two things at once.
    //It is the DECISION of the ⚠️ paragraph at the top of this file: a zero
    //written by hand is inside the documented meaning of the parameter and
    //stays honoured; only values that do not READ fall back to 15 s.
    //And it is what stops every case above from being satisfied by an input
    //that is simply never read: hasChanged() re-reads the configuration on
    //each turn, so the same twenty turns that poll nothing at 15 s poll twenty
    //times at 0. Without this the whole file would be vacuous.
    ASSERT_NE(nullptr, makeProbe({{ "period", "200" }}));

    ASSERT_EQ(0, pollsOver(1)) << "read before its period had elapsed";

    ASSERT_TRUE(probe->set_param("period", "0"));
    EXPECT_EQ(kLoopTurns, pollsOver(kLoopTurns))
            << "a period that IS due never reached readValue()";
}
