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
/*******************************************************************************
 * T3.25 (B2) - the SECOND belt: the IO itself, with the API boundary bypassed.
 *
 * core/SetStateGarbage_test drives the same defect through set_state and pins
 * what the API boundary refuses. This file calls set_value() directly, the way
 * every IN-PROCESS caller does - a rule action, a scenario step, a Lua binding -
 * and pins what Utils::from_string() alone buys: a DEFINED duration instead of
 * whatever was on the stack.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS IS A SEPARATE BINARY, AND IT IS NOT COSMETIC
 * ---------------------------------------------------------------------------
 * Before the fix these two cases do not merely fail, they KILL THE PROCESS.
 * Measured on this tree, characterization commit, `make check`:
 *
 *     core/SetStateGarbage_test ... exit status 139   (SIGSEGV)
 *
 * The indeterminate `int v` read at OutputShutter.cpp:113-114 came out as
 * -1857613792 on that run; ImpulseUp() stores it in impulse_action_time, and
 * OutputShutter.cpp:216-227 then arms Timer::singleShot() on a NEGATIVE delay,
 * which fires on the next turn of the libuv loop - after the fixture has
 * destroyed the IO. Six later cases of that binary never ran at all.
 *
 * That is exactly the "false green by death of the binary" the series keeps
 * hitting: no FAILED line is printed for what never ran, so counting FAILED
 * lines would have reported a smaller red set than the truth. Splitting the
 * two crashing cases out means the death can only ever hide THEM, and the
 * control stays what it must be - the CXXLD line plus the EXIT CODE, never a
 * FAILED count.
 *
 * This fixture is CoreFixture, not the JSON API harness: it never runs the
 * libuv loop, so an armed singleShot is created and abandoned rather than
 * fired into a destroyed object.
 ******************************************************************************/

#include "CalaosCoreFixture.h"

#include "IOFactory.h"
#include "OutputShutter.h"
#include "OutputLightDimmer.h"
#include "libuvw.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class T325bShutter: public OutputShutter
{
public:
    T325bShutter(Params &p): OutputShutter(p) {}

    int upPulses = 0;

protected:
    void setOutputUp(bool e) override { if (e) upPulses++; }
    void setOutputDown(bool) override {}
};

class T325bDimmer: public OutputLightDimmer
{
public:
    T325bDimmer(Params &p): OutputLightDimmer(p) {}

    int lastRealValue = -1;

protected:
    bool set_value_real(int val) override { lastRealValue = val; return true; }
};

void registerT325bIos()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T325bShutter", [](Params &p) -> IOBase * { return new T325bShutter(p); });
    IOFactory::Instance().RegisterClass(
        "T325bDimmer", [](Params &p) -> IOBase * { return new T325bDimmer(p); });
}

int armedTimerCount()
{
    int count = 0;

    uvw::Loop::getDefault()->walk([&count](uvw::BaseHandle &handle)
    {
        if (handle.type() == uvw::HandleType::TIMER &&
            handle.active() && !handle.closing())
            count++;
    });

    return count;
}

}

class ImpulseGarbageIoTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        registerT325bIos();
        CoreFixture::SetUp();
        loadConfig();
    }

    std::string caseName() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }

    T325bShutter *makeShutter()
    {
        Params p = {{ "type", "T325bShutter" },
                    { "id", "t325b_shutter_" + caseName() },
                    { "name", "Shutter under test" },
                    { "time", "60" },
                    { "impulse_time", "0" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return dynamic_cast<T325bShutter *>(createIO(p));
    }

    T325bDimmer *makeDimmer()
    {
        Params p = {{ "type", "T325bDimmer" },
                    { "id", "t325b_dimmer_" + caseName() },
                    { "name", "Dimmer under test" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return dynamic_cast<T325bDimmer *>(createIO(p));
    }
};

TEST_F(ImpulseGarbageIoTest, AWellFormedImpulseIsUntouched)
{
    //GREEN BEFORE AND AFTER, the control of this binary.
    T325bShutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_TRUE(sh->set_value(std::string("impulse up 500")));
    EXPECT_EQ("impulse up 500", sh->get_command_string());
    EXPECT_EQ(1, sh->upPulses);
}

TEST_F(ImpulseGarbageIoTest, AnImpulseWithNoDurationIsDefaultedToZero)
{
    //RED BEFORE THE FIX.
    //
    //⚠️ THE ONE NON-DETERMINISTIC RED OF THIS TICKET, said plainly: before the
    //fix `v` is INDETERMINATE, so this case is red for every value except the
    //one where the stack happens to hold 0. It is primed with a well formed
    //"impulse up 4242" first so the slot is very likely to read back something
    //unmistakable - measured, it read -1857613792, not 4242, because the
    //logging and cache work between the two calls rewrites the frame. Either
    //way it is not 0, and that is all the case needs; but an indeterminate
    //value cannot be asserted against with certainty, and pretending otherwise
    //would be the lie this whole ticket is about.
    T325bShutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_TRUE(sh->set_value(std::string("impulse up 4242")));
    ASSERT_EQ("impulse up 4242", sh->get_command_string());

    EXPECT_TRUE(sh->set_value(std::string("impulse up ")));
    EXPECT_EQ("impulse up 0", sh->get_command_string())
            << "the IO must fall back on a DEFINED duration";
}

TEST_F(ImpulseGarbageIoTest, ADimmerImpulseWithNoDurationArmsNoTimer)
{
    //RED BEFORE THE FIX, deterministically, and it is is_of_type() on trial
    //here rather than from_string(). OutputLightDimmer.cpp:203-212 asks
    //is_of_type<int>(tmp) BEFORE parsing; is_of_type<int>("") answers TRUE
    //today, so the empty tail takes the NUMERIC fork, from_string() leaves `t`
    //indeterminate and impulse(t) arms a libuv timer of t/1000 SECONDS.
    //Once is_of_type() tells the truth the empty tail takes the OTHER fork,
    //impulse_extended(""), which arms nothing at all for an empty pattern.
    //
    //The count is used as a DELTA: the fixture and libuv have handles of their
    //own and an absolute number would be meaningless.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    const int before = armedTimerCount();
    dim->set_value(std::string("impulse "));
    EXPECT_EQ(before, armedTimerCount())
            << "an impulse with no duration armed a timer on an indeterminate "
               "number of milliseconds";
}

TEST_F(ImpulseGarbageIoTest, AWellFormedDimmerImpulseStillArmsItsTimer)
{
    //GREEN BEFORE AND AFTER: the fix must not take away the impulse that works.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    const int before = armedTimerCount();
    dim->set_value(std::string("impulse 500"));
    EXPECT_EQ(before + 1, armedTimerCount());
}

TEST_F(ImpulseGarbageIoTest, ADimmerSetWithNoPercentDoesNotMoveTheLight)
{
    //RED BEFORE THE FIX.
    //
    //Same caveat as AnImpulseWithNoDurationIsDefaultedToZero: `int percent;`
    //is indeterminate and the clamp to [0,100] at OutputLightDimmer.cpp:144-145
    //hides that behind a PLAUSIBLE brightness - which is precisely what makes
    //this one hard to notice in the field. Primed with a well formed "set 40"
    //so the slot holds something recognisable.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_TRUE(dim->set_value(std::string("set 40")));
    ASSERT_EQ(40, dim->lastRealValue);

    EXPECT_TRUE(dim->set_value(std::string("set ")));
    EXPECT_EQ(0, dim->lastRealValue)
            << "the dimmer was driven to a level nobody asked for";
    EXPECT_EQ("set 0", dim->get_command_string());
}
