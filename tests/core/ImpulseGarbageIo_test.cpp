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
 * whatever was on the stack. ⚠️ Since T3.25a the shutter grammars go one step
 * further and REFUSE an argument that does not read as an int - see the note
 * on the renamed case below.
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

/* ⭐ T3.25 (review reserve 3). THE SENTINEL, and why 0 is never allowed to be
 * one here.
 *
 * 0x55555555 = 1431655765, the int form of the 0x5555 = 21845 that
 * tests/StringUtilsFromString_test seeds into every destination. Two distinct
 * jobs, both of them the same idea - "not written" must never be able to look
 * like "written zero":
 *
 *   - SENTINEL_VALUE seeds lastRealValue/lastDimUp, so "set_value_real() was
 *     never called at all" is distinguishable from "called with 0";
 *   - STACK_PAINT_BYTE is written over several kilobytes of stack BELOW the
 *     test frame just before the call, so that the UNINITIALISED `int percent`
 *     of OutputLightDimmer::set_value() reads back STACK_PAINT_INT instead of
 *     a plausible 0 when the T3.25 primitive is reverted.
 */
const int SENTINEL_VALUE   = 21845;         //0x5555
const int STACK_PAINT_BYTE = 0x55;
const int STACK_PAINT_INT  = 0x55555555;    //1431655765

void paintStackBelow()
{
    volatile unsigned char scratch[8192];
    for (size_t i = 0; i < sizeof(scratch); i++)
        scratch[i] = (unsigned char)STACK_PAINT_BYTE;

    //read it back through the volatile so that nothing here can be elided
    unsigned char acc = 0;
    for (size_t i = 0; i < sizeof(scratch); i++)
        acc = (unsigned char)(acc ^ scratch[i]);
    (void)acc;
}

class T325bDimmer: public OutputLightDimmer
{
public:
    T325bDimmer(Params &p): OutputLightDimmer(p) {}

    //Seeded with the sentinel, NEVER with 0 or -1: an oracle that expects 0 on
    //a destination that starts at 0 says nothing at all.
    int lastRealValue = SENTINEL_VALUE;
    int lastDimUp = SENTINEL_VALUE;

protected:
    bool set_value_real(int val) override { lastRealValue = val; return true; }
    bool set_dim_up_real(int percent) override { lastDimUp = percent; return true; }
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

/* ⚠️ RENAMED, AND THE CONTRACT UNDER IT CHANGED - T3.25a.
 *
 * T3.25 left this case reading AnImpulseWithNoDurationIsDefaultedToZero: the
 * IO fell back on a DEFINED duration of 0 while only the API boundary refused
 * the shape. T3.25a moved the rule down into the IO, where every in-process
 * writer meets it, and a command that lost its argument is now refused there
 * too. The old name is cited by docs/refactoring/T3.25.md; the rename is
 * recorded in docs/refactoring/T3.25a.md rather than left to be discovered.
 */
TEST_F(ImpulseGarbageIoTest, AnImpulseWithNoDurationIsRefusedByTheIo)
{
    //RED BEFORE T3.25, and its red was THE ONE NON-DETERMINISTIC RED of that
    //ticket: `v` was INDETERMINATE, so the case was red for every value except
    //the one where the stack happened to hold 0. It is still primed with a
    //well formed "impulse up 4242" first, for the same reason and for one
    //more: the published state must be the last command that WAS carried out.
    T325bShutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_TRUE(sh->set_value(std::string("impulse up 4242")));
    ASSERT_EQ("impulse up 4242", sh->get_command_string());

    EXPECT_FALSE(sh->set_value(std::string("impulse up ")))
            << "a command whose argument is missing must be refused by the IO "
               "as well, not carried out on a duration nobody chose";
    EXPECT_EQ("impulse up 4242", sh->get_command_string())
            << "the refused command overwrote the published state";
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

/*******************************************************************************
 * ⭐ THE TWO NON-DETERMINISTIC CASES OF THIS FILE, rewritten by the T3.25
 * review, which found the first version of them EMPTY BY CONSTRUCTION.
 *
 * WHAT WAS WRONG. The shipped `ADimmerSetWithNoPercentDoesNotMoveTheLight`
 * asserted EXPECT_EQ(0, lastRealValue) on a path where, with T3.25 reverted,
 * `int percent` is never written. It was red here and GREEN on the reviewer's
 * machine on the very same defective code. The rule the first version leaned
 * on - "at -O2 an uninitialised local reads 0" - IS NOT ONE. Measured across
 * two compilers, same program, five runs each:
 *
 *     g++ 12.2  -O0 -> 32766 x5      g++ 12.2  -O1 -> 0 x5
 *     g++ 12.2  -O2 -> 0 then 32648 x4
 *     g++ 16.2  -O2 -> 0 x5          g++ 16.2  -O3 -> 0 x5
 *
 * It is a PHENOMENON of the compiler and of the shape of the stack, not a
 * property, and an oracle that expects 0 on a non-initialisation path is empty
 * whenever the phenomenon lands on 0.
 *
 * WHAT IS DONE ABOUT IT, and what is honestly still not guaranteed:
 *   1. the stack below the test frame is PAINTED with 0x55 immediately before
 *      the call, so a slot that production never writes reads back
 *      STACK_PAINT_INT rather than whatever the previous call left;
 *   2. the discriminating assertion is the SENTINEL one - "the painted garbage
 *      did not surface" - and not the "== 0" one;
 *   3. the `up ` branch is added because it has NO CLAMP: `set ` clamps
 *      `percent` into [0,100] (OutputLightDimmer.cpp:145-147), which HIDES the
 *      garbage behind a plausible brightness, while `up ` hands the raw parsed
 *      int to set_dim_up_real().
 *
 * ⚠️ Still not a guaranteed red: a compiler that keeps `percent` in a register
 * never touches the painted stack at all, and nothing in the language lets a
 * test observe that. THE DETERMINISTIC PIN OF THIS EXACT DEFECT LIVES ON THE
 * PRIMITIVE - tests/StringUtilsFromString_test seeds 21845 and asserts
 * from_string("") answers false AND writes 0 - and that is the one that cannot
 * be argued with. These two cases are the IO-level corroboration of it, and
 * they are counted as two, not one, in T3.25 §8.7.
 ******************************************************************************/

TEST_F(ImpulseGarbageIoTest, ADimmerSetWithNoPercentDoesNotMoveTheLight)
{
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_TRUE(dim->set_value(std::string("set 40")));
    ASSERT_EQ(40, dim->lastRealValue);

    paintStackBelow();
    EXPECT_TRUE(dim->set_value(std::string("set ")));

    EXPECT_NE(SENTINEL_VALUE, dim->lastRealValue)
            << "set_value_real() was not called at all";
    EXPECT_EQ(0, dim->lastRealValue)
            << "the dimmer was driven to a level nobody asked for";
    EXPECT_EQ("set 0", dim->get_command_string());
}

TEST_F(ImpulseGarbageIoTest, ADimmerDimUpWithNoPercentDimsByNothing)
{
    //The same defect on the branch that does NOT clamp, so the garbage shows
    //as itself instead of as a plausible brightness. Measured on this tree
    //with T3.25 reverted and the stack painted: set_dim_up_real() received
    //1431655765 = STACK_PAINT_INT.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_TRUE(dim->set_value(std::string("up 10")));
    ASSERT_EQ(10, dim->lastDimUp);

    paintStackBelow();
    EXPECT_TRUE(dim->set_value(std::string("up ")));

    EXPECT_NE(SENTINEL_VALUE, dim->lastDimUp)
            << "set_dim_up_real() was not called at all";
    EXPECT_NE(STACK_PAINT_INT, dim->lastDimUp)
            << "the painted stack came straight back out of the parse: "
               "`percent` was never written, T3.25 has been reverted";
    EXPECT_EQ(0, dim->lastDimUp)
            << "the dimmer was dimmed up by an amount nobody asked for";
}

/*******************************************************************************
 * ⭐ T3.25 (review reserve 2) - THE OVERFLOW HALF OF THE is_of_type() CHANGE.
 *
 * §8.3 audited the BLANK half of the is_of_type() flip ("", " ", "-", "+").
 * The OVERFLOW half - "2147483648", "-2147483649", "99999999999" - flips at the
 * same 84 sites and had NO oracle anywhere in the tree at product level. The
 * primitive is pinned (StringUtilsFromString_test: IsOfTypeRefusesAnOverflow),
 * but before this block "a partial revert of T3.25 restricted to overflow" left
 * every product-level test green.
 *
 * These two are the reachable IO-grammar half of it. Both are DETERMINISTIC -
 * is_of_type() flips true -> false, nothing here reads an uninitialised local.
 ******************************************************************************/

TEST_F(ImpulseGarbageIoTest, ADimmerImpulseWithAnOverflowingDurationArmsNoTimer)
{
    //RED BEFORE THE FIX. is_of_type<int>("99999999999") answered TRUE, so the
    //numeric fork was taken, from_string() SATURATED the duration to INT_MAX
    //and impulse() armed a libuv timer of 2147483 s ~ 24.8 days with the light
    //switched ON. is_of_type() now refuses the token, the extended-pattern fork
    //is taken instead, and "99999999999" matches no arm of it: nothing is armed
    //and the light does not move.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    const int before = armedTimerCount();
    dim->set_value(std::string("impulse 99999999999"));
    EXPECT_EQ(before, armedTimerCount())
            << "a saturated impulse duration armed a timer for ~24.8 days";
}

TEST_F(ImpulseGarbageIoTest, ADimmerSetStateWithAnOverflowingPercentDoesNotMoveTheLight)
{
    //RED BEFORE THE FIX, and this one is the API-facing shape: "set_state
    //<n>" is what JsonApi's set_state hands to the IO verbatim - the boundary
    //guard added by this ticket only refuses a value ending on whitespace.
    //is_of_type<int>("99999999999") used to answer TRUE, from_string()
    //saturated to INT_MAX and the clamp turned that into 100: the dimmer was
    //driven to FULL BRIGHTNESS by a number that does not fit in an int.
    T325bDimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_TRUE(dim->set_value(std::string("set 40")));
    ASSERT_EQ("set 40", dim->get_command_string());
    ASSERT_EQ(40, dim->lastRealValue);

    dim->set_value(std::string("set_state 99999999999"));

    EXPECT_EQ("set 40", dim->get_command_string())
            << "an out-of-range set_state was saturated to INT_MAX and clamped "
               "to 100: the light went to full";
    EXPECT_EQ(40, dim->lastRealValue);
}
