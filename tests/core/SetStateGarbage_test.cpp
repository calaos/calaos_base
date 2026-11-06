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
 * T3.25 (B) - THE PRODUCT, not the function.
 *
 * tests/StringUtilsFromString_test pins Utils::from_string() itself. That is
 * not enough and the ticket says why: 312 call sites ignore its return code,
 * so a suite that only exercises the function leaves every one of them without
 * an oracle - mutating an IO would keep it green. This file drives the real
 * `set_state` command, over the real WebSocket transport, against a real
 * OutputShutter and a real OutputLightDimmer.
 *
 * ---------------------------------------------------------------------------
 * THE CHAIN, link by link (measured on this tree, 2026-08-25)
 * ---------------------------------------------------------------------------
 *  1. JsonApiHandlerWS.cpp:197 routes "set_state". It carries NO scopeDenied()
 *     - the seven that do are set_param, del_param, audio_db, set_timerange,
 *     eventlog, register_push and settings. JsonApiHandlerHttp.cpp:162 has no
 *     scope layer at all. The prerequisite is therefore: be authenticated.
 *     Nothing more, service-scoped sessions included.
 *  2. JsonApi.cpp:774/777 decodeSetState() hands io->set_value() the CLIENT
 *     STRING, verbatim.
 *  3. OutputShutter.cpp:110-116: val.compare(0, 11, "impulse up ") matches,
 *     val.erase(0, 11) leaves "", `int v;` is uninitialised, from_string("", v)
 *     returns TRUE WITHOUT WRITING, and ImpulseUp(v) runs on whatever was on
 *     the stack.
 *  4. OutputShutter.cpp:154-160: impulse_action_time = ms, then
 *     cmd_state = "impulse up " + to_string(impulse_action_time) and
 *     updateCache(). The arbitrary number is PUBLISHED and CACHED, which is
 *     also what makes it observable from here - get_command_string() returns
 *     cmd_state verbatim.
 *  5. OutputShutter.cpp:216-227: `impulse_action_time + impulse_time <
 *     time * 1000` decides whether a stop timer is armed at all. A large value
 *     makes it false: NO STOP TIMER, and the impulse degenerates into a full
 *     travel of the shutter.
 *
 * The payload is {"value": "impulse up "} - the prefix and nothing else.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS RED HERE BEFORE THE FIX
 * ---------------------------------------------------------------------------
 *  - AMalformedImpulseIsRefusedAtTheApiBoundary        (deterministic red)
 *  - TheStateCacheNeverPublishesAnArbitraryDuration    (deterministic red)
 *  - AMalformedDimmerSetIsRefusedAtTheApiBoundary      (deterministic red)
 *  - AValueEndingInWhitespaceIsRefusedForEveryIoType   (deterministic red)
 * Every red here is deterministic: they assert what the SERVER ANSWERED and
 * that the published state was left alone, never the arbitrary value itself.
 *
 * ⚠️ THIS BINARY DIES ON THE CHARACTERIZATION COMMIT, exit status 139, and it
 * is a CONSEQUENCE OF THE DEFECT rather than a fixture bug. Measured on this
 * tree: the two malformed impulses store a large NEGATIVE impulse_action_time,
 * OutputShutter.cpp:216-227 arms Timer::singleShot() on that negative delay,
 * the fixture then destroys the IO in TearDown() and pumps the loop - and the
 * abandoned timer fires into freed memory at the start of the NEXT case. Five
 * of the eight cases below therefore never ran on the characterization commit.
 *
 * THAT IS THE "false green by death of the binary" THE SERIES KEEPS HITTING:
 * nothing prints a FAILED line for what never ran, so counting FAILED lines
 * would have reported 2 reds where the truth is "2 red and 5 unknown". The
 * only control used here is the CXXLD line plus the EXIT CODE, never a count.
 * Once the fix lands, no arbitrary duration is stored, no negative timer is
 * armed, and the binary runs to completion - which is itself an assertion.
 *
 * ⚠️ The cases that call set_value() DIRECTLY live in a separate binary,
 * core/ImpulseGarbageIo_test, so that this death cannot hide them too. Read
 * that file's header before touching either.
 *
 * ⚠️ ORACLE, and the fixture mistake NOT made here: "the shutter did not move"
 * would be worthless - it cannot tell v = 0 from v = 2^31. Every case below
 * asserts the DURATION that was armed, read back from cmd_state, which embeds
 * to_string(impulse_action_time) character for character.
 *
 * ⚠️ NOT THIS TICKET: OutputShutter.cpp:119 erases 11 characters off the 13 of
 * "impulse down ", so ImpulseDown() never sees the duration it was given.
 * That is T3.34, it is NOT merged on this tree, and nothing here touches it.
 * AnImpulseDownCarriesADefinedDurationEitherWay is written to hold both before
 * and after T3.34 lands, on purpose.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "IOFactory.h"
#include "ListeRoom.h"
#include "OutputShutter.h"
#include "OutputLightDimmer.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//OutputShutter is concrete but drives nothing; the two hooks are counted so a
//case can also say whether the relay was actually pulled.
class T325Shutter: public OutputShutter
{
public:
    T325Shutter(Params &p): OutputShutter(p) {}

    int upPulses = 0;
    int downPulses = 0;

protected:
    void setOutputUp(bool e) override { if (e) upPulses++; }
    void setOutputDown(bool e) override { if (e) downPulses++; }
};

//OutputLightDimmer is abstract: set_value_real() is pure virtual.
class T325Dimmer: public OutputLightDimmer
{
public:
    T325Dimmer(Params &p): OutputLightDimmer(p) {}

    int lastRealValue = -1;

protected:
    bool set_value_real(int val) override { lastRealValue = val; return true; }
};

//IOFactory::RegisterClass is first-wins and has no unregister, so exactly once
//per process.
void registerT325Ios()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T325Shutter", [](Params &p) -> IOBase * { return new T325Shutter(p); });
    IOFactory::Instance().RegisterClass(
        "T325Dimmer", [](Params &p) -> IOBase * { return new T325Dimmer(p); });
}

}

class SetStateGarbageTest: public JsonApiCharacterizationTest
{
protected:
    void SetUp() override
    {
        registerT325Ios();
        JsonApiCharacterizationTest::SetUp();

        //Config's IO state cache is process wide and never cleared, and
        //OutputShutter's constructor reads it back under "<id>_<type>". Ids
        //are unique per case anyway (see makeShutter/makeDimmer), this is the
        //belt.
        forgetIOState(shutterId());
        forgetIOState(dimmerId());
        forgetIOState(stringId());

        loadConfig();
    }

    //One id per test case: two OutputShutters sharing an id would share the
    //process-wide state cache and make the assertions order dependent.
    std::string caseName() const
    {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }
    std::string shutterId() const { return "t325_shutter_" + caseName(); }
    std::string dimmerId() const { return "t325_dimmer_" + caseName(); }
    std::string stringId() const { return "t325_string_" + caseName(); }

    /* time="0" ON PURPOSE, and it is not cosmetic.
     *
     * OutputShutter.cpp:216-227 arms the stop timer with
     * Timer::singleShot(_t, mem_fun(this, &Stop)) when
     * impulse_action_time + impulse_time < time * 1000. singleShot() is
     * FIRE AND FORGET: the slot holds `this`, nothing cancels it, and it
     * outlives the IO. This fixture destroys every IO in TearDown() and then
     * pumps the libuv loop, so any timer still armed fires into freed memory
     * and takes the binary down with SIGSEGV - measured, repeatedly, on this
     * suite before and after the fix. That is a PRE-EXISTING use-after-free in
     * OutputShutter, not something this ticket introduced and not something it
     * fixes; it is written up as a finding.
     *
     * With time="0" the condition above is false for every non-negative
     * duration, so no timer is ever armed and the suite measures what it is
     * here to measure. The oracle is cmd_state, which records the duration
     * regardless of whether a timer was armed.
     */
    T325Shutter *makeShutter()
    {
        Params p = {{ "type", "T325Shutter" },
                    { "id", shutterId() },
                    { "name", "Shutter under test" },
                    { "time", "0" },
                    { "impulse_time", "0" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return dynamic_cast<T325Shutter *>(createIO(p));
    }

    T325Dimmer *makeDimmer()
    {
        Params p = {{ "type", "T325Dimmer" },
                    { "id", dimmerId() },
                    { "name", "Dimmer under test" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return dynamic_cast<T325Dimmer *>(createIO(p));
    }

    IOBase *makeStringIo()
    {
        Params p = {{ "type", "InternalString" },
                    { "id", stringId() },
                    { "name", "String under test" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    //One authenticated set_state over the real WS transport. Returns the
    //"success" field the server answered.
    std::string setState(const std::string &id, const std::string &value)
    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "set_state" },
                     { "msg_id", "1" },
                     { "data", {{ "id", id }, { "value", value }} }});

        if (ws.count() != 1u)
            return "<no answer>";
        return ws.lastData().value("success", std::string("<no success key>"));
    }
};

/*******************************************************************************
 * The remotely reachable chain
 ******************************************************************************/

TEST_F(SetStateGarbageTest, AWellFormedImpulseStillWorks)
{
    //GREEN BEFORE AND AFTER. The control: neither the API guard nor the
    //from_string correction may take away the command that works.
    T325Shutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("true", setState(shutterId(), "impulse up 500"));
    EXPECT_EQ("impulse up 500", sh->get_command_string())
            << "a well formed impulse must arm exactly the duration asked for";
    EXPECT_EQ(1, sh->upPulses);
}

TEST_F(SetStateGarbageTest, AMalformedImpulseIsRefusedAtTheApiBoundary)
{
    //RED BEFORE THE FIX, deterministically: today the server answers
    //{"success":"true"} and cmd_state reads "impulse up <arbitrary int>" -
    //never the empty string.
    //
    //This is the DEFECT OF THE TICKET, by the only path a client has:
    //{"value": "impulse up "} - the prefix, and nothing after it.
    T325Shutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("false", setState(shutterId(), "impulse up "))
            << "a command whose argument is missing must be refused, not "
               "executed on an indeterminate value";
    EXPECT_EQ("", sh->get_command_string())
            << "the shutter must not have been commanded at all; anything of "
               "the form 'impulse up N' here means the request went through";
    EXPECT_EQ(0, sh->upPulses) << "no relay may have been pulled";
}

TEST_F(SetStateGarbageTest, TheStateCacheNeverPublishesAnArbitraryDuration)
{
    //RED BEFORE THE FIX, deterministically. ImpulseUp() ends with
    //updateCache(), which writes cmd_state into Config's state cache - so the
    //arbitrary number is not only acted upon, it is PERSISTED and served back
    //to every client that asks for the IO state afterwards.
    T325Shutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    setState(shutterId(), "impulse up 500");
    ASSERT_EQ("impulse up 500", sh->get_command_string());

    setState(shutterId(), "impulse up ");

    //Whatever the contract ends up being, the published state must still be
    //the last WELL FORMED command; it must never advertise a duration nobody
    //asked for.
    EXPECT_EQ("impulse up 500", sh->get_command_string())
            << "the refused command overwrote the published state with an "
               "arbitrary duration";

    Params cached;
    Config::Instance().ReadValueParams(shutterId() + "_T325Shutter", cached);
    EXPECT_EQ("impulse up 500", cached["cmd_state"])
            << "and the same arbitrary duration reached the state cache";
}

TEST_F(SetStateGarbageTest, AnImpulseDownCarriesADefinedDurationEitherWay)
{
    //GREEN BEFORE AND AFTER, and deliberately written to survive T3.34.
    //OutputShutter.cpp:119 erases 11 characters off the 13 of "impulse down ",
    //so from_string() is handed "n 500" and writes 0 - the duration is lost.
    //T3.34 owns that line and will make this read "impulse down 500". What
    //THIS ticket guarantees, before and after T3.34, is only that the duration
    //is DEFINED and never arbitrary.
    T325Shutter *sh = makeShutter();
    ASSERT_NE(nullptr, sh);

    EXPECT_EQ("true", setState(shutterId(), "impulse down 500"));

    const std::string cmd = sh->get_command_string();
    EXPECT_TRUE(cmd == "impulse down 0" || cmd == "impulse down 500")
            << "expected the T3.34 defect ('impulse down 0') or its fix "
               "('impulse down 500'), got: " << cmd;
}

/*******************************************************************************
 * The same family, on a dimmer
 ******************************************************************************/

TEST_F(SetStateGarbageTest, AWellFormedDimmerSetStillWorks)
{
    //GREEN BEFORE AND AFTER, the dimmer's control.
    T325Dimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_EQ("true", setState(dimmerId(), "set 40"));
    EXPECT_EQ("set 40", dim->get_command_string());
    EXPECT_EQ(40, dim->lastRealValue);
}

TEST_F(SetStateGarbageTest, AMalformedDimmerSetIsRefusedAtTheApiBoundary)
{
    //RED BEFORE THE FIX, deterministically: today "set " reaches
    //OutputLightDimmer.cpp:141-152, `int percent;` is uninitialised, the
    //clamp to [0,100] hides the garbage behind a PLAUSIBLE brightness, and
    //set_value_real() is called with it. A light really does change level, to
    //a value nobody chose.
    T325Dimmer *dim = makeDimmer();
    ASSERT_NE(nullptr, dim);

    EXPECT_EQ("false", setState(dimmerId(), "set "));
    EXPECT_EQ(-1, dim->lastRealValue)
            << "the dimmer was driven to a level no one asked for";
    EXPECT_EQ("", dim->get_command_string());
}

/*******************************************************************************
 * The cost of the boundary guard, stated rather than discovered
 ******************************************************************************/

TEST_F(SetStateGarbageTest, AStringIoStillTakesAnyValueThatIsNotTruncated)
{
    //GREEN BEFORE AND AFTER. The guard added at the API boundary must not cost
    //an ordinary text value its content.
    IOBase *s = makeStringIo();
    ASSERT_NE(nullptr, s);

    EXPECT_EQ("true", setState(stringId(), "hello world"));
    EXPECT_EQ("hello world", s->get_value_string());

    EXPECT_EQ("true", setState(stringId(), " leading and interior  spaces"));
    EXPECT_EQ(" leading and interior  spaces", s->get_value_string());
}

TEST_F(SetStateGarbageTest, AValueEndingInWhitespaceIsRefusedForEveryIoType)
{
    //RED BEFORE THE FIX, and this is the DELIBERATE COST of the boundary
    //guard, pinned here so it can never be discovered by a user instead.
    //A set_state value that ends on its separator is a command that lost its
    //argument, in every grammar of the IO tree ("impulse up ", "set ",
    //"set off ", "up ", "down "). Refusing the shape at the boundary is what
    //keeps the guard working if a future caller reintroduces the pattern -
    //and the price is that a TEXT variable can no longer be set to a value
    //ending in a space through this command.
    IOBase *s = makeStringIo();
    ASSERT_NE(nullptr, s);

    EXPECT_EQ("true", setState(stringId(), "kept"));
    ASSERT_EQ("kept", s->get_value_string());

    EXPECT_EQ("false", setState(stringId(), "trailing space "));
    EXPECT_EQ("kept", s->get_value_string())
            << "a refused set_state must leave the value alone";
}
