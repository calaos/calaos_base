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
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/*----------------------------------------------------------------------------
 * T3.53 - THE DALI UDP REPLY PAIR: (string command, string result).
 *
 * WagoMap.h:81-82 declare the slot and the signal that carry a UDP reply from
 * calaos_wago back to a DALI ballast object:
 *
 *     typedef sigc::slot  <void, bool, string, string> WagoUdp_cb;
 *     typedef sigc::signal<void, bool, string, string> WagoUdp_signal;
 *
 * ⭐ WHY THIS PAIR IS NOT LIKE THE THREE THE WAGO CHAIN ALREADY CLOSED.
 * T3.31, T3.46 and T3.50 each closed a pair whose two members had DIFFERENT
 * types that converted into one another (UWord <-> int): silent, but a
 * compiler COULD have spoken had the types been distinct. Here both members
 * are std::string - the SAME type. There is no conversion to diagnose, so no
 * compiler, no flag and no type-based static analyser will ever say anything
 * about a permutation. Typing by role is the only remedy there is.
 *
 * ⚠️ WHAT KIND OF ORACLE EACH CASE IS - read this before quoting any result.
 *
 *   - The cases named *Refuses* / *TheReplySlotCarries* are a COMPILATION
 *     property reported through an executable oracle: std::is_invocable_v and
 *     std::is_same_v turn "can a caller still hand this slot its two strings
 *     the wrong way round?" into a value gtest can print. They prove a
 *     permuted call no longer TYPE-CHECKS. They prove nothing about what any
 *     callback then does. Every EXPECT_FALSE has its EXPECT_TRUE control IN
 *     THE SAME CASE, never one case away.
 *
 *   - The cases under WagoUdpReplyExerciseTest are BEHAVIOURAL and they run
 *     production objects: a WODali and a WODaliRVB built by the production
 *     constructor through IOFactory, handed a reply through the production
 *     WagoMap::udpRequest_cb(), and read back through the public
 *     get_value_string(). "Linked" is not "exercised" (F-LINK-1); these are
 *     exercised.
 *
 * ⭐ AND THE MEASUREMENT THAT DECIDES WHAT MAY BE ASKED OF A TEST HERE - it
 * is the OPPOSITE of what T3.50 measured on its own pair, so it is spelled
 * out rather than inherited. BOTH members are READ, by every live
 * implementation:
 *
 *     WODali.cpp:86     cInfoDom("output") << "Error with request " << command;
 *     WODali.cpp:92     if (command.find("WAGO_DALI_GET") != string::npos)
 *     WODali.cpp:95     split(result, tokens);
 *     WODaliRVB.cpp:94  ... << command;   :100  split(result, tokens);
 *     WODaliRVB.cpp:115 ... << command;   :121  split(result, tokens);
 *     WODaliRVB.cpp:136 ... << command;   :142  split(result, tokens);
 *
 * ⇒ A permutation here is NOT a semantic no-op. It CHANGES THE PROGRAM, and
 *   the two behavioural cases below separate the two programs for real:
 *     - WODali: the reply is searched for "WAGO_DALI_GET" instead of the
 *       command, the GET branch is never taken again, and the ballast keeps
 *       its startup value forever - silently.
 *     - WODaliRVB: split() is handed the COMMAND, so tokens[2] is the DALI
 *       ADDRESS of the channel and from_string() stores that address as the
 *       channel LEVEL. A live wrong value, not a lost update.
 *
 * ⚠️ WHAT THIS FILE DOES NOT CLOSE, declared and not promised:
 *   - The five implementations are PRIVATE members, so no probe may name them
 *     through a pointer to member the way core/WagoReadReply_test does for
 *     T3.50. What closes their signature is the sigc::mem_fun registration in
 *     their own .cpp (WODali.cpp:62, WODaliRVB.cpp:73/:75/:77): converting a
 *     mem_functor into a WagoUdp_cb requires the member to be callable with
 *     the typed argument list, so a re-widening there is a BUILD FAILURE.
 *     Measured, mutations M3..M6 of docs/refactoring/T3.53.md - not assumed.
 *   - ⛔ WODaliRVB::WagoUDPCommand_cb (WODaliRVB.h:41, WODaliRVB.cpp:164) is
 *     registered NOWHERE: it is the fifth definition and there are only four
 *     sigc::mem_fun. Its address is taken by no line of the tree (measured).
 *     It is typed here for the contract, but nothing - not a build, not a
 *     test - can catch a permutation inside it. Said plainly.
 *   - W3, wrapping the WRONG variable: UdpCommand(result) type-checks and
 *     always will. Intrinsic to wrapping.
 *   - W7, unwrapping into an untyped layer: `string s = command.v;` inside an
 *     implementation type-checks.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"
#include "ColorUtils.h"
#include "IOFactory.h"
#include "WagoMap.h"

#include <gtest/gtest.h>
#include <sigc++/sigc++.h>
#include <string>
#include <type_traits>

using Calaos::WagoUdp_cb;
using Calaos::WagoUdp_signal;

namespace
{

/* ⭐ The two argument types are read OUT OF THE PRODUCTION SLOT itself rather
 * than named. That is deliberate and it is what lets this file compile - and
 * fail - on master, where the roles have no types yet. A file that stopped
 * compiling on master would be an ABSENT test, which is the worst of the
 * false greens, not a red one. */
template<class S> struct UdpSlotArgs;

template<class R, class A1, class A2, class A3>
struct UdpSlotArgs<sigc::slot<R, A1, A2, A3>>
{
    using Cmd = A2;
    using Res = A3;
};

template<class S> struct UdpSignalArgs;

template<class R, class A1, class A2, class A3>
struct UdpSignalArgs<sigc::signal<R, A1, A2, A3>>
{
    using Cmd = A2;
    using Res = A3;
};

using CmdArg = UdpSlotArgs<WagoUdp_cb>::Cmd;
using ResArg = UdpSlotArgs<WagoUdp_cb>::Res;
using SigCmdArg = UdpSignalArgs<WagoUdp_signal>::Cmd;
using SigResArg = UdpSignalArgs<WagoUdp_signal>::Res;

/* Positive control for is_base_of_v: a trait answering FALSE to everything
 * would let the W4 line below pass for free. Kept in this file, used inside
 * the case that leans on it. */
struct AProbeBase { };
struct AProbeDerived: AProbeBase { };

/* Positive control for the W6 line: a wrapper that DOES leak back to its
 * scalar. Without it an is_convertible_v that answered FALSE to every
 * wrapper-to-string question would make W6 pass for free. */
struct ALeakyWrapper
{
    std::string v;
    operator std::string() const { return v; }
};

/* Positive control for the W1 line: a wrapper whose constructor is NOT
 * explicit, so a bare string converts into it on its own - exactly the
 * workaround the real wrapper must not allow. */
struct AnImplicitWrapper
{
    std::string v;
    AnImplicitWrapper(std::string s): v(std::move(s)) {}
};

} //namespace

/*----------------------------------------------------------------------------
 * ⭐ THE DEFECT ITSELF, stated as a type identity.
 *--------------------------------------------------------------------------*/

TEST(WagoUdpReply, TheReplySlotCarriesTwoRolesNotTwoStrings)
{
    /* The control, and it is what makes the line after it mean something: a
     * slot spelled with two bare strings really does take them either way
     * round, in total silence. This one is a LOCAL type, so it says the same
     * thing before and after the typing - it is the yardstick, not the
     * measurement. */
    using ABarePair = sigc::slot<void, bool, std::string, std::string>;
    EXPECT_TRUE((std::is_invocable_v<ABarePair, bool, std::string, std::string>))
        << "a bare (string, string) slot no longer accepts two strings - the "
           "yardstick this whole file is measured against is broken";
    EXPECT_TRUE((std::is_same_v<UdpSlotArgs<ABarePair>::Cmd,
                                UdpSlotArgs<ABarePair>::Res>))
        << "the two members of a bare pair must be the SAME type - that is "
           "the whole reason no compiler can ever diagnose a permutation";

    /* ⭐ And the measurement. On master both are std::string and this fails:
     * WagoUdp_cb carries a command and a result of the same type, in a row,
     * and WagoMap.cpp:439 hands them over positionally. */
    EXPECT_FALSE((std::is_same_v<CmdArg, ResArg>))
        << "WagoUdp_cb (WagoMap.h:81) still carries two parameters of the "
           "SAME type in a row: the command and the result of a DALI UDP "
           "reply are interchangeable and no compiler can ever say so";

    EXPECT_FALSE((std::is_same_v<SigCmdArg, SigResArg>))
        << "WagoUdp_signal (WagoMap.h:82) still carries two parameters of the "
           "SAME type in a row";
}

/*----------------------------------------------------------------------------
 * The slot and the signal, asked whether a permuted call still type-checks.
 *--------------------------------------------------------------------------*/

TEST(WagoUdpReply, TheUdpReplySlotRefusesABareCommandAndResultPair)
{
    //The control. Without it a slot that accepted NOTHING would pass every
    //EXPECT_FALSE of this case for free.
    EXPECT_TRUE((std::is_invocable_v<WagoUdp_cb, bool, CmdArg, ResArg>))
        << "the correctly ordered call must still be accepted - every "
           "EXPECT_FALSE in this case is passing for free";

    EXPECT_FALSE((std::is_invocable_v<WagoUdp_cb, bool, std::string,
                                      std::string>))
        << "WagoUdp_cb still takes two bare strings: at WagoMap.cpp:439 the "
           "command and the result of a UDP reply are interchangeable in "
           "total silence";

    //⭐ The permutation, literally: the two arguments swapped.
    EXPECT_FALSE((std::is_invocable_v<WagoUdp_cb, bool, ResArg, CmdArg>))
        << "the command and the result are still interchangeable";
}

TEST(WagoUdpReply, TheUdpReplySignalRefusesABareCommandAndResultPair)
{
    EXPECT_TRUE((std::is_invocable_v<WagoUdp_signal, bool, SigCmdArg,
                                     SigResArg>))
        << "the correctly ordered emission must still be accepted - every "
           "EXPECT_FALSE in this case is passing for free";

    EXPECT_FALSE((std::is_invocable_v<WagoUdp_signal, bool, std::string,
                                      std::string>))
        << "WagoUdp_signal still takes two bare strings, and WagoMap.cpp:439 "
           "is the single line that emits through it";

    EXPECT_FALSE((std::is_invocable_v<WagoUdp_signal, bool, SigResArg,
                                      SigCmdArg>))
        << "the emission site can still hand the result over as the command";
}

/*----------------------------------------------------------------------------
 * The shape of the wrappers, asked of the type system. Same four workarounds
 * T3.31 measured (W1, W4, W6) plus the narrowing blind spot.
 *--------------------------------------------------------------------------*/

TEST(WagoUdpReply, TheWrapperShapeIsWhatCloses)
{
    //W1 - explicit constructor, so a bare string is not a command.
    EXPECT_TRUE((std::is_constructible_v<CmdArg, std::string>))
        << "the role type cannot even be built from a string - the W1 line "
           "below is passing for free";
    EXPECT_TRUE((std::is_convertible_v<std::string, AnImplicitWrapper>))
        << "is_convertible_v answers FALSE for every string-to-wrapper "
           "question here - the two W1 lines below prove nothing";
    EXPECT_FALSE((std::is_convertible_v<std::string, CmdArg>));
    EXPECT_FALSE((std::is_convertible_v<std::string, ResArg>));

    //W4 - no common base, so neither role stands in for the other.
    EXPECT_TRUE((std::is_base_of_v<AProbeBase, AProbeDerived>))
        << "is_base_of_v answers FALSE for everything here - the two W4 lines "
           "below are passing for free";
    EXPECT_FALSE((std::is_base_of_v<CmdArg, ResArg>));
    EXPECT_FALSE((std::is_base_of_v<ResArg, CmdArg>));

    //W6 - and no way back to a bare string without naming .v.
    EXPECT_TRUE((std::is_convertible_v<ALeakyWrapper, std::string>))
        << "is_convertible_v answers FALSE for every wrapper-to-string "
           "question here - the two W6 lines below prove nothing";
    EXPECT_FALSE((std::is_convertible_v<CmdArg, std::string>));
    EXPECT_FALSE((std::is_convertible_v<ResArg, std::string>));
}

/*----------------------------------------------------------------------------
 * ⭐ THE EXERCISE - production DALI objects, actually run. There was NOT ONE
 * DALI test in this tree before this file (measured: zero *Dali* under
 * tests/), so everything below starts from a blank page.
 *
 * The path is the production one end to end: the constructor queues a
 * "WAGO_DALI_GET ..." through WagoMap::SendUDPCommand() with a sigc::mem_fun
 * on its own callback, and WagoMap::udpRequest_cb() - a public entry point -
 * pops that command and emits (status, command, result) at WagoMap.cpp:439.
 *
 * ⚠️ The two strings of every case are deliberately FAR APART and neither is
 * empty: a fixture where the command and the reply look alike ("cmd"/"cmd2")
 * makes the permutation invisible, and that is the single most repeated
 * mistake of this campaign. TheFixtureItselfDiscriminates below measures that
 * the two orders really do produce different answers, so the claim is not a
 * reading of the fixture.
 *
 * ⚠️ One host per case, TEST-NET-1 (RFC 5737): syntactically valid, never
 * local, so bind() fails with EADDRNOTAVAIL and this binary can never collide
 * with a parallel `make check -j` peer over WAGO_LISTEN_PORT. Same reasoning
 * as core/WagoPortDefault_test and core/WagoReadReply_test.
 *--------------------------------------------------------------------------*/

namespace
{

/* What the ballast answers on the wire. tokens[1] drives WODali (on/off) and
 * tokens[2] drives each WODaliRVB channel (level). NOTHING here looks like a
 * command. */
const char *const DALI_REPLY_ON = "REPLY 1 77";
const char *const DALI_REPLY_OFF = "REPLY 0 77";

} //namespace

class WagoUdpReplyExerciseTest: public CalaosTest::CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    std::string hostFor(const std::string &suffix) const
    {
        return "192.0.2." + suffix;
    }

    Calaos::IOBase *makeDali(const std::string &host, const std::string &id,
                             const std::string &line,
                             const std::string &address)
    {
        Params p = {{ "type", "WODali" },
                    { "id", id },
                    { "name", "T3.53 dali ballast" },
                    { "host", host },
                    { "line", line },
                    { "address", address },
                    { "group", "0" },
                    { "fade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    Calaos::IOBase *makeDaliRVB(const std::string &host, const std::string &id)
    {
        Params p = {{ "type", "WODaliRVB" },
                    { "id", id },
                    { "name", "T3.53 dali rgb ballast" },
                    { "host", host },
                    { "rline", "1" }, { "raddress", "61" }, { "rgroup", "0" },
                    { "rfade_time", "1" },
                    { "gline", "1" }, { "gaddress", "62" }, { "ggroup", "0" },
                    { "gfade_time", "1" },
                    { "bline", "1" }, { "baddress", "63" }, { "bgroup", "0" },
                    { "bfade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }
};

/* ⭐ The DALI GET branch of WODali::WagoUDPCommand_cb is REACHED and taken.
 *
 * Under a permutation of WagoMap.cpp:439 the callback searches the REPLY for
 * "WAGO_DALI_GET", never finds it, and the ballast keeps its startup value -
 * so this case goes RED on the permuted program. That is what makes it a real
 * behavioural oracle and not a compilation one. */
TEST_F(WagoUdpReplyExerciseTest, ADaliGetReplyReallyReachesTheBallast)
{
    const std::string host = hostFor("51");
    Calaos::IOBase *dali = makeDali(host, "t353_dali_on", "1", "61");
    ASSERT_NE(nullptr, dali);

    //Sentinel: the startup value must NOT already be the answer, or the
    //expectation below would pass without the callback ever running.
    ASSERT_EQ("0", dali->get_value_string());

    Calaos::WagoMap::Instance(host, 502).udpRequest_cb(true, DALI_REPLY_ON);

    EXPECT_EQ("100", dali->get_value_string())
        << "WODali::WagoUDPCommand_cb did not take its WAGO_DALI_GET branch: "
           "either it was never reached, or it was handed the reply as the "
           "command (the permutation of WagoMap.cpp:439)";
}

/* The other side of the same branch, so "it became 100" cannot be satisfied
 * by a callback that always assigns 100. */
TEST_F(WagoUdpReplyExerciseTest, ADaliGetReplyOfZeroLeavesTheBallastOff)
{
    const std::string host = hostFor("52");
    Calaos::IOBase *dali = makeDali(host, "t353_dali_off", "1", "61");
    ASSERT_NE(nullptr, dali);

    Calaos::WagoMap::Instance(host, 502).udpRequest_cb(true, DALI_REPLY_OFF);

    EXPECT_EQ("0", dali->get_value_string())
        << "a reply whose second token is 0 must leave the ballast off";
}

/* The control on `status`. Without it "the value moved" could be satisfied by
 * a callback that ignores the status flag entirely. */
TEST_F(WagoUdpReplyExerciseTest, AFailedDaliReplyLeavesTheBallastAlone)
{
    const std::string host = hostFor("53");
    Calaos::IOBase *dali = makeDali(host, "t353_dali_failed", "1", "61");
    ASSERT_NE(nullptr, dali);

    const std::string before = dali->get_value_string();

    Calaos::WagoMap::Instance(host, 502).udpRequest_cb(false, DALI_REPLY_ON);

    EXPECT_EQ(before, dali->get_value_string())
        << "a failed UDP reply must not move the ballast";
}

/* ⭐ THE ONE THAT SHOWS THE PERMUTATION IS A LIVE WRONG VALUE.
 *
 * Each channel of WODaliRVB reads tokens[2] of the RESULT as its level. Under
 * a permutation split() is handed the COMMAND instead - "WAGO_DALI_GET 1 61"
 * - whose tokens[2] is the DALI ADDRESS of the channel. The address is then
 * stored as the level: red 61 instead of 77, green 62 instead of 77, blue 63
 * instead of 77. Both programs produce a colour; they produce DIFFERENT
 * colours, which is exactly why a behavioural test can separate them here and
 * could not on the T3.50 pair. */
TEST_F(WagoUdpReplyExerciseTest, ADaliRVBChannelTakesItsLevelFromTheResult)
{
    const std::string host = hostFor("54");
    Calaos::IOBase *rgb = makeDaliRVB(host, "t353_dali_rgb");
    ASSERT_NE(nullptr, rgb);

    //Not yet ON: the three channels are still -1, checkReadState() refuses.
    ASSERT_EQ("0", rgb->get_value_string());

    Calaos::WagoMap &map = Calaos::WagoMap::Instance(host, 502);

    //The three queued GETs are answered in the order they were queued:
    //red (address 61), green (62), blue (63). All three get the SAME reply,
    //so a channel that read its own address would stand out immediately.
    map.udpRequest_cb(true, DALI_REPLY_ON);
    map.udpRequest_cb(true, DALI_REPLY_ON);
    map.udpRequest_cb(true, DALI_REPLY_ON);

    //Built exactly as WODaliRVB::checkReadState() builds it, from the level
    //77 that lives in the RESULT. (The /1000 on blue is production's, not a
    //typo of this test - see WODaliRVB.cpp:157.)
    //(ColorValue takes ints; production truncates exactly the same way.)
    ColorValue expected(int(77.0 * 255. / 100.), int(77.0 * 255. / 100.),
                        int(77.0 * 255. / 1000.));

    EXPECT_EQ(expected.toString(), rgb->get_value_string())
        << "the three WODaliRVB channels did not take their level from the "
           "RESULT: under the permutation of WagoMap.cpp:439 they take it "
           "from the COMMAND and store the DALI address as the level";
}

/* ⭐ THE FIXTURE'S OWN CONTROL - the guard against the poor fixture that has
 * bitten this campaign twelve times. It measures that the correct order and
 * the permuted order really do produce different observable answers, so the
 * case above is capable of going red at all. It says the same thing before
 * and after the typing: it is arithmetic on the fixture, not on the tree. */
TEST_F(WagoUdpReplyExerciseTest, TheFixtureItselfDiscriminatesTheTwoOrders)
{
    //What the correct order yields: the level 77, from the RESULT.
    ColorValue correct(int(77.0 * 255. / 100.), int(77.0 * 255. / 100.),
                       int(77.0 * 255. / 1000.));
    //What the permuted order yields: the three DALI addresses, from the
    //COMMAND "WAGO_DALI_GET 1 6x".
    ColorValue permuted(int(61.0 * 255. / 100.), int(62.0 * 255. / 100.),
                        int(63.0 * 255. / 1000.));

    EXPECT_NE(correct.toString(), permuted.toString())
        << "the fixture cannot tell the two orders apart - every behavioural "
           "case in this file is passing for free";

    //And the WODali side of the same guard: the reply must not itself look
    //like a command, or the GET branch would be taken either way round.
    EXPECT_EQ(std::string::npos,
              std::string(DALI_REPLY_ON).find("WAGO_DALI_GET"))
        << "the reply used by this file contains WAGO_DALI_GET, so the "
           "permuted program would take the GET branch too and the WODali "
           "cases would pass for free";
    EXPECT_NE(std::string::npos,
              std::string("WAGO_DALI_GET 1 61").find("WAGO_DALI_GET"));
}
