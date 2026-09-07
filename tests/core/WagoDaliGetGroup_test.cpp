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
 * the address in the write and AFTER it in the read. That is the PLC
 * protocol, verified in the eight programs of the calaos_wago repository, and
 * the write must not be "fixed" to match the read.
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
 *   - Nothing here reaches a PLC. That the flag is read at all, and what a
 *     753-647 then answers, needs the hardware.
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
