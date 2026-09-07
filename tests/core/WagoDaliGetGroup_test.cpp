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
 * THE DALI STATE READ AND THE GROUP FLAG IT DROPS.
 *
 * A ballast addressed as a DALI GROUP reads back as another ballast's state,
 * every time the server starts, because the state read never told the PLC it
 * was asking about a group:
 *
 *   write   WODali.cpp   "WAGO_DALI_SET <line> <group> <address> <val> <fade>"
 *   read    WODali.cpp   "WAGO_DALI_GET <line> <address>"          <- no group
 *
 * THE FLAG IS NOT IN THE SAME PLACE IN THE TWO COMMANDS. It comes BEFORE
 * the address in the write and AFTER it in the read. That is the 3.0 PLC
 * protocol, and the write must not be "fixed" to match the read.
 *
 * AND THE READ ONLY TAKES THE FLAG FROM 3.0 ON. Before that program the
 * handler derives its flag from the SECOND field, the one that already
 * carries the short address, and never looks at what it derived - so no
 * position of that frame is a flag. GET_PARAM_DINT also has
 * no guard for a parameter it cannot find, so a third field is not ignored:
 * the pre-3.0 programs read that position as the DMX read address, where a
 * real 0 or 1 takes a DMX fixture out of its own branch. The server therefore
 * asks WAGO_GET_VERSION once per PLC and appends the flag only for 3.0 and
 * later. ⛔ AN UNKNOWN VERSION KEEPS THE TWO PARAMETER FORM, which is what
 * every program has always been sent: a lost datagram must not be able to
 * turn itself into a broken DMX read.
 *
 * THE COMPARISON IS THE PROOF, NOT EITHER HALF OF IT. "The frame has three
 * fields" is green on a program that always sends three. Every version case
 * below is therefore paired with its opposite regime on identical IO
 * parameters, and the unknown-version case is the one that matters most,
 * because it is the path no one plays by hand.
 *
 * WHAT THIS FILE MEASURES, AND WHY IT IS THE ONLY THING THAT COULD.
 * The defect lives in the CONTENT OF THE FRAME THE SERVER EMITS, not in what
 * it does with a reply. core/WagoUdpReply_test drives the reply path and is
 * GREEN on the broken program: a suite that only watched the tokens[1] == "0"
 * conversion would measure nothing of this. Every case here therefore reads
 * the frame the constructor actually queued for the wire, and asserts on the
 * POSITION of each token, never on the presence of a character.
 *
 * WHY POSITION AND NOT PRESENCE. `line` is 1 in nearly every installation
 * and a group flag is 0 or 1, so "the frame contains 1" is already true on the
 * broken program. TheFixtureSeparatesTheAddressFromTheGroup below measures
 * that, so the claim is not a reading of this comment.
 *
 * WHY THE ADDRESS IS 7 AND NOT 1. Swapping the address and the group in the
 * frame changes NO VALUE when the address is itself 0 or 1 - the mutation
 * would be invisible and every case would pass on the permuted program. Each
 * address here is far from both flag values.
 *
 * HOW THE EMITTED FRAME IS READ. WagoMap keeps what it is about to put on
 * the wire in a protected queue and hands it back to nobody until a reply pops
 * it; there is no public accessor. Naming the member through a derived class
 * yields a pointer-to-member of the BASE, which reads the real production
 * queue without adding a line to production. The alternative - asserting on
 * the reply path - is exactly the blind oracle described above.
 *
 * One host per case, TEST-NET-1 (RFC 5737): syntactically valid, never
 * local, so bind() fails with EADDRNOTAVAIL and this binary can never collide
 * with a parallel `make check -j` peer over WAGO_LISTEN_PORT. A fresh host is
 * also a fresh WagoMap singleton, hence a queue holding this case's frames and
 * nothing else. Same reasoning as core/WagoUdpReply_test.
 *
 * WHAT THIS FILE DOES NOT CLOSE, declared and not promised:
 *   - Nothing here reaches a PLC. That the flag is read at all, that a real
 *     automate answers WAGO_GET_VERSION at all, and what a 753-647 then
 *     answers, all need the hardware.
 *   - A pre-3.0 PLC still never learns about a group: its read has no field
 *     in which a flag is ever read back. Withholding the flag keeps that
 *     installation exactly as it is today, it does not make its group reads
 *     work - and no other frame shape would either.
 *   - The three `*line` parameters of WODaliRVB still have no default, so an
 *     io.xml without them emits an empty field the way an absent group used
 *     to. Out of this file's reach and untouched by it.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"
#include "IOFactory.h"
#include "WagoMap.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

/* The production queue of frames waiting for the wire, read through a
 * pointer-to-member formed on a derived class. Nothing is ever constructed
 * from this type. */
struct WagoMapProbe: Calaos::WagoMap
{
    static std::vector<std::string> frames(const Calaos::WagoMap &map)
    {
        auto queued = map.*(&WagoMapProbe::udp_commands);
        std::vector<std::string> out;
        while (!queued.empty())
        {
            out.push_back(queued.front().udp_command);
            queued.pop();
        }
        return out;
    }

    /* The PRODUCTION timeout entry point, not a hand written equivalent of
     * it. What the 2s timer calls when a datagram never comes back is this
     * and only this, so a case that drives it measures the real silent-PLC
     * path instead of a story about it. */
    static void fireTimeout(Calaos::WagoMap &map)
    {
        (map.*(&WagoMapProbe::UDPCommandTimeout_cb))();
    }
};

/* Splits on EVERY space and keeps empty fields, unlike the production split():
 * an empty parameter is the very thing the group default exists to prevent,
 * and a splitter that swallowed it would hide it. */
std::vector<std::string> fields(const std::string &frame)
{
    std::vector<std::string> out;
    std::string::size_type start = 0;
    for (;;)
    {
        std::string::size_type sp = frame.find(' ', start);
        if (sp == std::string::npos)
        {
            out.push_back(frame.substr(start));
            return out;
        }
        out.push_back(frame.substr(start, sp - start));
        start = sp + 1;
    }
}

} //namespace

class WagoDaliGetGroupTest: public CalaosTest::CoreFixture
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

    std::vector<std::string> framesOn(const std::string &host) const
    {
        return WagoMapProbe::frames(Calaos::WagoMap::Instance(host, 502));
    }

    //The version query the WagoMap constructor puts at the head of the queue,
    //answered the way an incoming datagram answers it.
    void answerVersion(const std::string &host, const std::string &reply) const
    {
        Calaos::WagoMap::Instance(host, 502).udpRequest_cb(true, reply);
    }

    void answerVersion(const std::string &host, int major, int minor) const
    {
        answerVersion(host, "WAGO_GET_VERSION " + std::to_string(major) + "." +
                            std::to_string(minor) + " 750-849");
    }

    //A PLC that says nothing at all: the 2s timer fires, through the very
    //function it calls in production.
    void timeoutOnce(const std::string &host) const
    {
        WagoMapProbe::fireTimeout(Calaos::WagoMap::Instance(host, 502));
    }

    //Answers every frame still queued for this host, as a timing out PLC
    //does, so that each pending callback runs exactly once.
    void timeoutEverything(const std::string &host) const
    {
        for (size_t guard = 0; guard < 32 && !framesOn(host).empty(); guard++)
            timeoutOnce(host);
    }

    int barrier() const { return Calaos::StartReadRules::Instance().pendingIOCount(); }

    //withGroup false leaves the key out entirely, as an io.xml written before
    //the parameter existed does.
    Calaos::IOBase *makeDali(const std::string &host, const std::string &id,
                             const std::string &line, const std::string &address,
                             const std::string &group, bool withGroup = true)
    {
        Params p = {{ "type", "WODali" },
                    { "id", id },
                    { "name", "dali ballast" },
                    { "host", host },
                    { "line", line },
                    { "address", address },
                    { "fade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        if (withGroup) p.Add("group", group);
        return createIO(p);
    }

    Calaos::IOBase *makeDaliRVB(const std::string &host, const std::string &id,
                                bool withGroups = true)
    {
        Params p = {{ "type", "WODaliRVB" },
                    { "id", id },
                    { "name", "dali rgb ballast" },
                    { "host", host },
                    { "rline", "1" }, { "raddress", "11" }, { "rfade_time", "1" },
                    { "gline", "2" }, { "gaddress", "22" }, { "gfade_time", "1" },
                    { "bline", "3" }, { "baddress", "33" }, { "bfade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        if (withGroups)
        {
            p.Add("rgroup", "1");
            p.Add("ggroup", "0");
            p.Add("bgroup", "1");
        }
        return createIO(p);
    }
};

/*----------------------------------------------------------------------------
 * THE DEFECT, on the frame the constructor queues for the wire.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliGetGroupTest, ADaliStateReadNamesTheGroupInThirdPosition)
{
    const std::string host = hostFor("61");
    ASSERT_NE(nullptr, makeDali(host, "dali_grouped", "1", "7", "1"));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size())
        << "the constructor did not queue exactly one frame";

    const std::vector<std::string> f = fields(queued[0]);
    ASSERT_EQ(4u, f.size())
        << "the state read is still asking with two parameters: '" << queued[0]
        << "'. The PLC then reads a BALLAST at that address instead of the "
           "GROUP, and the ballast shows the state of something else";
    EXPECT_EQ("WAGO_DALI_GET", f[0]);
    EXPECT_EQ("1", f[1]) << "the line left its first position";
    EXPECT_EQ("7", f[2]) << "the address left its second position";
    EXPECT_EQ("1", f[3]) << "the group flag is not in third position";

    EXPECT_EQ("WAGO_DALI_GET 1 7 1", queued[0]);
}

/* The other side of the pair, so "the frame ends in 1" cannot be satisfied by
 * a constructor that appends a constant. */
TEST_F(WagoDaliGetGroupTest, ADaliStateReadOfAnUngroupedBallastNamesAZero)
{
    const std::string host = hostFor("62");
    ASSERT_NE(nullptr, makeDali(host, "dali_plain", "1", "7", "0"));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size());
    EXPECT_EQ("WAGO_DALI_GET 1 7 0", queued[0]);
}

/* THE TWO PROGRAMS SEPARATED. Two ballasts differing ONLY by their group
 * parameter must not ask the PLC the same question - which is precisely what
 * they did, and why the thirteen ungrouped ballasts of the installation were
 * right while the fourteenth was wrong. */
TEST_F(WagoDaliGetGroupTest, TheGroupIsTheOnlyThingThatSeparatesTwoStateReads)
{
    const std::string grouped = hostFor("63");
    const std::string plain = hostFor("64");
    ASSERT_NE(nullptr, makeDali(grouped, "dali_pair_grouped", "1", "7", "1"));
    ASSERT_NE(nullptr, makeDali(plain, "dali_pair_plain", "1", "7", "0"));
    answerVersion(grouped, 3, 0);
    answerVersion(plain, 3, 0);

    const std::vector<std::string> a = framesOn(grouped);
    const std::vector<std::string> b = framesOn(plain);
    ASSERT_EQ(1u, a.size());
    ASSERT_EQ(1u, b.size());

    EXPECT_NE(a[0], b[0])
        << "a grouped ballast and an ungrouped one at the same address emit "
           "the SAME state read: the group is carried by nothing";

    const std::vector<std::string> fa = fields(a[0]);
    const std::vector<std::string> fb = fields(b[0]);
    ASSERT_EQ(4u, fa.size());
    ASSERT_EQ(4u, fb.size());
    EXPECT_EQ(fa[2], fb[2]) << "the two frames must differ by the flag ALONE";
    EXPECT_NE(fa[3], fb[3]);
}

/* An io.xml written before the parameter existed carries no group at all. It
 * must become a "0", never an empty field: the PLC parser converts an EMPTY
 * parameter by running its digit loop to INT_TO_BYTE(-1) = 255 and reading
 * bytes past the string, so the flag it derives is arbitrary. */
TEST_F(WagoDaliGetGroupTest, ADaliStateReadWithNoGroupParamCarriesAZeroNotAHole)
{
    const std::string host = hostFor("65");
    ASSERT_NE(nullptr, makeDali(host, "dali_nogroup", "1", "7", "", false));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size());

    const std::vector<std::string> f = fields(queued[0]);
    ASSERT_EQ(4u, f.size());
    for (size_t i = 0; i < f.size(); i++)
        EXPECT_FALSE(f[i].empty())
            << "field " << i << " of '" << queued[0] << "' is empty";
    EXPECT_EQ("0", f[3]);
}

/* The same hole on the WRITE frame, which reads the very same key without a
 * guard. Nothing about the write's parameter ORDER is asserted here - it is
 * correct and must stay as it is. */
TEST_F(WagoDaliGetGroupTest, ADaliWriteWithNoGroupParamCarriesNoEmptyField)
{
    const std::string host = hostFor("66");
    Calaos::IOBase *dali = makeDali(host, "dali_nogroup_write", "1", "7", "", false);
    ASSERT_NE(nullptr, dali);
    answerVersion(host, 3, 0);

    ASSERT_TRUE(dali->set_value(std::string("set 50")));

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(2u, queued.size()) << "the write queued no frame";
    EXPECT_EQ("WAGO_DALI_SET 1 0 7 50 1", queued[1]);
}

/*----------------------------------------------------------------------------
 * The three channels of an RGB ballast, which have the same defect three
 * times over.
 *--------------------------------------------------------------------------*/

/* The three flags are DIFFERENT and so are the three lines and the three
 * addresses: a channel that read a neighbour's group, or that put its flag
 * where its line goes, shows up here rather than cancelling out. */
TEST_F(WagoDaliGetGroupTest, EachRVBChannelNamesItsOwnGroupInThirdPosition)
{
    const std::string host = hostFor("67");
    ASSERT_NE(nullptr, makeDaliRVB(host, "dali_rgb"));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(3u, queued.size())
        << "the constructor did not queue one frame per channel";

    EXPECT_EQ("WAGO_DALI_GET 1 11 1", queued[0]) << "red";
    EXPECT_EQ("WAGO_DALI_GET 2 22 0", queued[1]) << "green";
    EXPECT_EQ("WAGO_DALI_GET 3 33 1", queued[2]) << "blue";

    for (size_t c = 0; c < queued.size(); c++)
    {
        const std::vector<std::string> f = fields(queued[c]);
        ASSERT_EQ(4u, f.size()) << "channel " << c << ": '" << queued[c] << "'";
        EXPECT_EQ("WAGO_DALI_GET", f[0]);
    }
}

/* A control, green on both programs by design: it pins the LINE and the
 * ADDRESS to the two positions they already occupy, and asserts nothing about
 * how many fields follow. It is what separates "the flag is missing" from
 * "the flag was put where the address goes" - both of which redden the case
 * above, and only one of which reddens this one. */
TEST_F(WagoDaliGetGroupTest, AnRVBChannelKeepsItsAddressInSecondPosition)
{
    const std::string host = hostFor("69");
    ASSERT_NE(nullptr, makeDaliRVB(host, "dali_rgb_positions"));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(3u, queued.size());

    const char *const lines[] = { "1", "2", "3" };
    const char *const addresses[] = { "11", "22", "33" };

    for (size_t c = 0; c < queued.size(); c++)
    {
        const std::vector<std::string> f = fields(queued[c]);
        ASSERT_LE(3u, f.size()) << "channel " << c << ": '" << queued[c] << "'";
        EXPECT_EQ(lines[c], f[1]) << "channel " << c << " lost its line";
        EXPECT_EQ(addresses[c], f[2]) << "channel " << c << " lost its address";
    }
}

TEST_F(WagoDaliGetGroupTest, AnRVBWithNoGroupParamsCarriesZerosNotHoles)
{
    const std::string host = hostFor("68");
    ASSERT_NE(nullptr, makeDaliRVB(host, "dali_rgb_nogroup", false));
    answerVersion(host, 3, 0);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(3u, queued.size());

    for (size_t c = 0; c < queued.size(); c++)
    {
        const std::vector<std::string> f = fields(queued[c]);
        ASSERT_EQ(4u, f.size()) << "channel " << c << ": '" << queued[c] << "'";
        for (size_t i = 0; i < f.size(); i++)
            EXPECT_FALSE(f[i].empty())
                << "channel " << c << " field " << i << ": '" << queued[c] << "'";
        EXPECT_EQ("0", f[3]) << "channel " << c;
    }
}

/*----------------------------------------------------------------------------
 * THE FIXTURE'S OWN CONTROL. Arithmetic on the values this file chose, so
 * it says the same thing on both programs and cannot be satisfied by the
 * tree.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliGetGroupTest, TheFixtureSeparatesTheAddressFromTheGroup)
{
    //The permutation this file must be able to see: the flag put where the
    //address goes. With the address 7 the two frames differ; with an address
    //of 0 or 1 they would be the SAME STRING and every case above would pass
    //on the permuted program.
    EXPECT_NE(std::string("WAGO_DALI_GET 1 7 1"),
              std::string("WAGO_DALI_GET 1 1 7"))
        << "the chosen address does not separate the two orders - every case "
           "in this file is blind to the permutation";
    EXPECT_EQ(std::string("WAGO_DALI_GET 1 1 1"),
              std::string("WAGO_DALI_GET 1 1 1"))
        << "the yardstick itself is broken";

    //And why presence proves nothing: the frame of the BROKEN program already
    //contains the flag's own spelling, at the line's position.
    const std::vector<std::string> broken = fields("WAGO_DALI_GET 1 7");
    ASSERT_EQ(3u, broken.size());
    EXPECT_EQ("1", broken[1])
        << "a case asserting that the frame CONTAINS \"1\" would pass on the "
           "program this file exists to reject";

    //The empty field the group default exists to prevent must be visible to
    //the splitter used above; production's split() drops it.
    const std::vector<std::string> hole = fields("WAGO_DALI_GET 1 7 ");
    ASSERT_EQ(4u, hole.size());
    EXPECT_TRUE(hole[3].empty())
        << "the splitter swallows empty fields, so the two ...NotAHole cases "
           "are passing for free";
}

/*----------------------------------------------------------------------------
 * THE VERSION GATE. Which frame leaves depends on the program the PLC runs,
 * and the two regimes are always measured against each other on identical IO
 * parameters - a case reading one regime alone proves nothing.
 *--------------------------------------------------------------------------*/

/* Nothing goes out before the PLC has said what it runs. The only frame the
 * constructor may have queued is the version query itself. */
TEST_F(WagoDaliGetGroupTest, AStateReadWaitsForThePlcToSayWhatItRuns)
{
    const std::string host = hostFor("70");
    ASSERT_NE(nullptr, makeDali(host, "dali_await", "1", "7", "1"));

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size())
        << "a DALI read left before the version was known";
    EXPECT_EQ("WAGO_GET_VERSION", queued[0]);
}

/* ⭐ THE PAIR THAT CARRIES THE PROOF. Two ballasts with the SAME line, the
 * same address and the same group, on two PLCs differing only by their
 * announced version. A program that always appends the flag, and a program
 * that never does, each redden exactly one of these two expectations. */
TEST_F(WagoDaliGetGroupTest, TheAnnouncedVersionIsWhatDecidesTheThirdField)
{
    const std::string modern = hostFor("71");
    const std::string legacy = hostFor("72");
    ASSERT_NE(nullptr, makeDali(modern, "dali_v30", "1", "7", "1"));
    ASSERT_NE(nullptr, makeDali(legacy, "dali_v23", "1", "7", "1"));

    answerVersion(modern, 3, 0);
    answerVersion(legacy, 2, 3);

    const std::vector<std::string> a = framesOn(modern);
    const std::vector<std::string> b = framesOn(legacy);
    ASSERT_EQ(1u, a.size());
    ASSERT_EQ(1u, b.size());

    EXPECT_EQ("WAGO_DALI_GET 1 7 1", a[0]) << "a 3.0 PLC was not told the group";
    EXPECT_EQ("WAGO_DALI_GET 1 7", b[0])
        << "a 2.3 PLC was sent a third parameter. It does not ignore it: it "
           "reads that position as the DMX read address";

    //Same IO, so the frames may differ by the trailing field and by nothing
    //else - a version gate that also moved the address would pass the two
    //string comparisons above and still be wrong.
    const std::vector<std::string> fa = fields(a[0]);
    const std::vector<std::string> fb = fields(b[0]);
    ASSERT_EQ(4u, fa.size());
    ASSERT_EQ(3u, fb.size());
    for (size_t i = 0; i < fb.size(); i++)
        EXPECT_EQ(fb[i], fa[i]) << "field " << i << " moved between the two regimes";
}

/* ⭐⭐ THE PATH NOBODY PLAYS: the PLC never answers. Driven through the
 * production timeout, not through a hand written stand-in for it. */
TEST_F(WagoDaliGetGroupTest, APlcThatNeverAnswersKeepsTheTwoParameterRead)
{
    const std::string host = hostFor("73");
    ASSERT_NE(nullptr, makeDali(host, "dali_silent", "1", "7", "1"));

    timeoutOnce(host);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size()) << "the read never left after the timeout";
    EXPECT_EQ("WAGO_DALI_GET 1 7", queued[0])
        << "a version that never arrived was taken for a 3.0";
}

/* A reply that is not a version. Three shapes, three hosts: a truncated
 * frame, a version field that is not a number, and an answer to something
 * else entirely landing in the version slot. */
TEST_F(WagoDaliGetGroupTest, AMalformedVersionReplyKeepsTheTwoParameterRead)
{
    const char *const replies[] = { "WAGO_GET_VERSION",
                                    "WAGO_GET_VERSION x.y 750-849",
                                    "WAGO_HEARTBEAT 1" };
    const char *const suffixes[] = { "74", "75", "76" };

    for (size_t i = 0; i < 3; i++)
    {
        const std::string host = hostFor(suffixes[i]);
        ASSERT_NE(nullptr, makeDali(host, std::string("dali_bad") + suffixes[i],
                                    "1", "7", "1"));
        answerVersion(host, replies[i]);

        const std::vector<std::string> queued = framesOn(host);
        ASSERT_EQ(1u, queued.size()) << "reply '" << replies[i] << "'";
        EXPECT_EQ("WAGO_DALI_GET 1 7", queued[0])
            << "'" << replies[i] << "' was read as a usable version";
    }
}

/* The threshold itself, and its direction. 2.9 is below 3.0 even though its
 * minor is the larger one, and a version above 3.0 stays above it. A
 * comparison inverted, or one that forgot the major, reddens here. */
TEST_F(WagoDaliGetGroupTest, ThreeZeroIsTheFirstVersionToldTheGroup)
{
    struct { const char *suffix; int major, minor; bool group; } cases[] = {
        { "80", 1, 7, false },
        { "81", 2, 3, false },
        { "82", 2, 9, false },
        { "83", 3, 0, true },
        { "84", 3, 1, true },
        { "85", 4, 0, true },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const std::string host = hostFor(cases[i].suffix);
        ASSERT_NE(nullptr, makeDali(host, std::string("dali_v") + cases[i].suffix,
                                    "1", "7", "1"));
        answerVersion(host, cases[i].major, cases[i].minor);

        const std::vector<std::string> queued = framesOn(host);
        ASSERT_EQ(1u, queued.size());
        EXPECT_EQ(cases[i].group ? "WAGO_DALI_GET 1 7 1" : "WAGO_DALI_GET 1 7",
                  queued[0])
            << "version " << cases[i].major << "." << cases[i].minor;
    }
}

/* The three channels follow the same gate together: a version read once per
 * PLC must reach all three, not just the first one queued. */
TEST_F(WagoDaliGetGroupTest, TheThreeRVBChannelsFollowTheSameVersionGate)
{
    const std::string modern = hostFor("86");
    const std::string legacy = hostFor("87");
    ASSERT_NE(nullptr, makeDaliRVB(modern, "dali_rgb_v30"));
    ASSERT_NE(nullptr, makeDaliRVB(legacy, "dali_rgb_v23"));

    answerVersion(modern, 3, 0);
    answerVersion(legacy, 2, 3);

    const std::vector<std::string> a = framesOn(modern);
    const std::vector<std::string> b = framesOn(legacy);
    ASSERT_EQ(3u, a.size());
    ASSERT_EQ(3u, b.size());

    EXPECT_EQ("WAGO_DALI_GET 1 11 1", a[0]) << "red";
    EXPECT_EQ("WAGO_DALI_GET 2 22 0", a[1]) << "green";
    EXPECT_EQ("WAGO_DALI_GET 3 33 1", a[2]) << "blue";

    EXPECT_EQ("WAGO_DALI_GET 1 11", b[0]) << "red";
    EXPECT_EQ("WAGO_DALI_GET 2 22", b[1]) << "green";
    EXPECT_EQ("WAGO_DALI_GET 3 33", b[2]) << "blue";
}

/*----------------------------------------------------------------------------
 * ⭐⭐ THE START BARRIER. WODali counts itself into StartReadRules before
 * handing its read over, and gets that count back from the callback alone.
 * Deferring the read puts a round trip between the two, so every path -
 * including the one where the version never comes - has to give the count
 * back or the server never runs its start rules.
 *--------------------------------------------------------------------------*/

/* The sensor itself, before it is used to prove anything. A counter that
 * never moved would make every case below green for the wrong reason. */
TEST_F(WagoDaliGetGroupTest, TheBarrierCounterIsAliveAndReturnsToItsLevel)
{
    const int before = barrier();
    Calaos::StartReadRules::Instance().addIO();
    ASSERT_EQ(before + 1, barrier()) << "the barrier does not count up";
    Calaos::StartReadRules::Instance().ioRead();
    EXPECT_EQ(before, barrier()) << "the barrier does not count back down";
}

TEST_F(WagoDaliGetGroupTest, TheBarrierBalancesWhenTheVersionArrives)
{
    const std::string host = hostFor("88");
    const int before = barrier();
    ASSERT_NE(nullptr, makeDali(host, "dali_bal_known", "1", "7", "1"));
    ASSERT_EQ(before + 1, barrier()) << "the constructor did not count itself in";

    answerVersion(host, 3, 0);
    EXPECT_EQ(before + 1, barrier())
        << "the version reply gave the count back on its own - the read has "
           "not happened yet";

    timeoutEverything(host);
    EXPECT_EQ(before, barrier()) << "the DALI read never gave its count back";
}

/* ⭐ THE CASE THAT MATTERS. A PLC that answers nothing at all: the version
 * times out, the deferred read still has to leave, and its own timeout has to
 * give the count back. A flush that dropped its pending list would leave the
 * server waiting for ever, with no other symptom. */
TEST_F(WagoDaliGetGroupTest, TheBarrierBalancesWhenTheVersionNeverArrives)
{
    const std::string host = hostFor("89");
    const int before = barrier();
    ASSERT_NE(nullptr, makeDali(host, "dali_bal_silent", "1", "7", "1"));
    ASSERT_EQ(before + 1, barrier());

    timeoutEverything(host);
    EXPECT_EQ(before, barrier())
        << "a PLC that never answered left the start barrier owing one read: "
           "the server would never run its start rules";
    EXPECT_TRUE(framesOn(host).empty()) << "a frame is still queued";
}

/* And the same for a malformed reply, which is the other way the version can
 * be unusable. */
TEST_F(WagoDaliGetGroupTest, TheBarrierBalancesOnAMalformedVersionReply)
{
    const std::string host = hostFor("90");
    const int before = barrier();
    ASSERT_NE(nullptr, makeDali(host, "dali_bal_bad", "1", "7", "1"));

    answerVersion(host, "WAGO_GET_VERSION x.y 750-849");
    timeoutEverything(host);
    EXPECT_EQ(before, barrier());
}

/* The three channels, which owe three counts and must give back three. A
 * flush that stopped at the first pending request balances the case above and
 * reddens this one. */
TEST_F(WagoDaliGetGroupTest, TheBarrierBalancesForAllThreeRVBChannels)
{
    const std::string host = hostFor("91");
    const int before = barrier();
    ASSERT_NE(nullptr, makeDaliRVB(host, "dali_rgb_bal"));
    ASSERT_EQ(before + 3, barrier()) << "the three channels did not count in";

    timeoutEverything(host);
    EXPECT_EQ(before, barrier())
        << "the three channels did not all give their count back";
}

/* An IO built AFTER the version is settled must not queue behind a flush that
 * already happened: its read goes out at once, and its count comes back the
 * same way. */
TEST_F(WagoDaliGetGroupTest, ABallastBuiltAfterTheVersionIsSettledStillBalances)
{
    const std::string host = hostFor("92");
    ASSERT_NE(nullptr, makeDali(host, "dali_first", "1", "7", "1"));
    answerVersion(host, 3, 0);
    timeoutEverything(host);

    const int before = barrier();
    ASSERT_NE(nullptr, makeDali(host, "dali_second", "1", "9", "1"));
    ASSERT_EQ(before + 1, barrier());

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size()) << "the second read waited for a version "
                                    "query that is already answered";
    EXPECT_EQ("WAGO_DALI_GET 1 9 1", queued[0]);

    timeoutEverything(host);
    EXPECT_EQ(before, barrier());
}
