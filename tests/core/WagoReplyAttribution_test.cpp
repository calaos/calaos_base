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
 * A UDP REPLY THAT ARRIVES LATE IS READ AS THE NEXT COMMAND'S ANSWER.
 *
 * WagoMap attributes an incoming datagram to the HEAD of its command queue and
 * to nothing else - by position, never by content. The version query the map
 * asks once per PLC expires after 2s; if the PLC answers after that, the head
 * is no longer the version query but the first DALI state read, and the
 * version string is handed to the ballast:
 *
 *   result  "WAGO_GET_VERSION 3.0 750-849"   3 tokens, tokens[1] = "3.0"
 *   WODali  tokens[1] != "0"  =>  value = 100
 *
 * The luminaire shows as ON. A DUPLICATED version datagram does the same thing
 * on a PLC that answered in time. Both are the symptom the group-flag work
 * exists to remove, on any ballast this time.
 *
 * WHAT THE FIX IS, AND WHAT IT IS NOT. The PLC names the command in its
 * answer - measured in the eight PLC programs, 1.7 through 3.0: a DALI read is
 * answered by "WAGO_DALI_GET <state> <level>" and the version query by
 * "WAGO_GET_VERSION <H>.<L> <model>". A datagram naming a DIFFERENT command
 * than a DALI read at the head therefore belongs to a request nobody is
 * waiting for any more, and is dropped.
 *
 * ONLY THE DALI READ IS GUARDED, and the boundary is measured here, not
 * asserted: a foreign datagram taken for a VERSION answer leaves the PLC on
 * "unknown", which is the two-parameter read every program has always been
 * sent, while one taken for a DALI STATE yields a plain 100. The degradation
 * only goes to the safe side in one direction, and this fix closes the other.
 * A datagram that names no command at all cannot be placed either way and
 * keeps the conduct it has always had - this file measures that too, so the
 * guard cannot be a guard that drops everything.
 *
 * THE START BARRIER IS WHAT MAKES A DROP DANGEROUS. Every WODali counts itself
 * into StartReadRules and gets that count back from its callback only. Drop a
 * datagram and the head still owes its callback; what gives it back is the 2s
 * timeout, so the guard must return BEFORE the timeout timer is cancelled.
 * TheDroppedDatagramLeavesTheHeadTimeoutArmed reads that timer through the
 * production arming path, and the barrier cases below read
 * StartReadRules::pendingIOCount() RELATIVELY, so a count left by a
 * neighbouring case cannot make them pass.
 *
 * WHY EVERY CASE THAT READS A BALLAST CARRIES A POSITIVE CONTROL. "The ballast
 * is off" is the ballast's STARTUP value: a case asserting only that is green
 * when no callback runs at all, which is the open defect of the neighbouring
 * suite. Every case here that reads a ballast value therefore also requires a
 * ballast to REACH "100" in that same case. The remaining cases read the
 * emitted frame or the barrier counter, where the startup value is not the
 * expected one and the trap does not apply.
 *
 * That claim is measured, not asserted: unplugging the conversion in
 * WODali::WagoUDPCommand_cb while leaving its ioRead() in place reddens every
 * case here that reads a ballast, and none of the others.
 *
 * WHY THE VALUES ARE 7, 77 AND 3.0. The DALI address is 7 and the reported
 * level 77, never 0 or 1: a permutation of the address with the group flag, or
 * of a state with a flag, changes no value when the operands are themselves
 * flag values. The version is 3.0 and 2.3, never 0.0.
 *
 * One host per case, TEST-NET-1 (RFC 5737): a fresh host is a fresh WagoMap
 * singleton, so each case owns its queue, and bind() can never collide with a
 * parallel `make check -j` peer over WAGO_LISTEN_PORT.
 *
 * WHAT THIS FILE DOES NOT CLOSE, declared and not promised:
 *   - Nothing here reaches a PLC. That a real automate ever answers after its
 *     2s, or duplicates a datagram, is not measured anywhere; the frequency of
 *     the window is unknown.
 *   - Attribution stays POSITIONAL. The guard refuses a datagram that names
 *     another command; it does not pair a reply with its request. Two DALI
 *     reads in flight still have no way of telling their answers apart, and
 *     nothing here changes that.
 *   - A datagram that names no command is still attributed to the head, as it
 *     always was.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"
#include "IOFactory.h"
#include "WagoMap.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

/* WagoMap's protected state and its protected entry points, reached through
 * pointers-to-member formed on a derived class. Nothing is ever constructed
 * from this type and no line is added to production for it. */
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

    //The PRODUCTION timeout entry point, not a hand written equivalent of it.
    static void fireTimeout(Calaos::WagoMap &map)
    {
        (map.*(&WagoMapProbe::UDPCommandTimeout_cb))();
    }

    /* The PRODUCTION send step, which is the only thing that arms the 2s
     * timeout in the server. A test that armed that timer itself would be
     * measuring its own fixture. */
    static void sendHead(Calaos::WagoMap &map)
    {
        (map.*(&WagoMapProbe::UDPCommand_cb))();
    }

    static bool timeoutArmed(const Calaos::WagoMap &map)
    {
        return map.*(&WagoMapProbe::udp_timeout_timer) != nullptr;
    }
};

//Splits on every space and keeps empty fields, unlike the production split().
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

//What a 3.0 PLC puts on the wire for its version query, model field included.
const char *const VERSION_REPLY_30 = "WAGO_GET_VERSION 3.0 750-849";
const char *const VERSION_REPLY_23 = "WAGO_GET_VERSION 2.3 750-849";

/* What the PLC really answers a DALI state read with: the command name, the
 * on/off state, then the level. Read out of the ST source of the eight
 * programs, not invented here. 77 is a level no flag and no address can be. */
const char *const DALI_REPLY_ON = "WAGO_DALI_GET 1 77";
const char *const DALI_REPLY_OFF = "WAGO_DALI_GET 0 0";

} //namespace

class WagoReplyAttributionTest: public CalaosTest::CoreFixture
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

    Calaos::WagoMap &mapOn(const std::string &host) const
    {
        return Calaos::WagoMap::Instance(host, 502);
    }

    std::vector<std::string> framesOn(const std::string &host) const
    {
        return WagoMapProbe::frames(mapOn(host));
    }

    //An incoming datagram, delivered the way the UDP socket delivers one.
    void datagram(const std::string &host, const std::string &payload) const
    {
        mapOn(host).udpRequest_cb(true, payload);
    }

    //A PLC that says nothing: the real 2s timer callback.
    void timeoutOnce(const std::string &host) const
    {
        WagoMapProbe::fireTimeout(mapOn(host));
    }

    int barrier() const { return Calaos::StartReadRules::Instance().pendingIOCount(); }

    Calaos::IOBase *makeDali(const std::string &host, const std::string &id,
                             const std::string &address, const std::string &group)
    {
        forgetIOState(id);
        Params p = {{ "type", "WODali" },
                    { "id", id },
                    { "name", "dali ballast" },
                    { "host", host },
                    { "line", "1" },
                    { "address", address },
                    { "group", group },
                    { "fade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    Calaos::IOBase *makeDaliRVB(const std::string &host, const std::string &id)
    {
        forgetIOState(id);
        Params p = {{ "type", "WODaliRVB" },
                    { "id", id },
                    { "name", "dali rgb ballast" },
                    { "host", host },
                    { "rline", "1" }, { "raddress", "7" }, { "rgroup", "0" }, { "rfade_time", "1" },
                    { "gline", "1" }, { "gaddress", "8" }, { "ggroup", "0" }, { "gfade_time", "1" },
                    { "bline", "1" }, { "baddress", "9" }, { "bgroup", "0" }, { "bfade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }
};

/*----------------------------------------------------------------------------
 * THE DEFECT: A VERSION ANSWER THAT ARRIVES AFTER ITS 2s.
 *--------------------------------------------------------------------------*/

TEST_F(WagoReplyAttributionTest, ALateVersionReplyIsNeverReadAsABallastState)
{
    const std::string host = hostFor("71");
    Calaos::IOBase *dali = makeDali(host, "t3164_late", "7", "0");
    ASSERT_NE(nullptr, dali);

    //The PLC stays silent long enough for the version query to expire, so the
    //DALI read becomes the head of the queue.
    timeoutOnce(host);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size())
        << "the expired version query did not leave the DALI read at the head";
    ASSERT_EQ("WAGO_DALI_GET 1 7", queued[0]);

    //Sentinel: the answer below must not already be the startup value.
    ASSERT_EQ("0", dali->get_value_string());

    datagram(host, VERSION_REPLY_30);

    EXPECT_EQ("0", dali->get_value_string())
        << "the version answer was attributed to the DALI read: its second "
           "token is '3.0', which is not \"0\", so the ballast reads as lit";

    const std::vector<std::string> still = framesOn(host);
    ASSERT_EQ(1u, still.size())
        << "the dropped datagram consumed the DALI read, which now waits for "
           "an answer that will never be attributed to it";
    EXPECT_EQ("WAGO_DALI_GET 1 7", still[0]);

    /* POSITIVE CONTROL. The same expression must move when the answer the head
     * is actually waiting for arrives - otherwise "it stayed at 0" would be
     * satisfied by a callback path that never runs. */
    datagram(host, DALI_REPLY_ON);
    EXPECT_EQ("100", dali->get_value_string())
        << "the genuine DALI answer did not reach the ballast either: this "
           "case measures nothing";
    EXPECT_TRUE(framesOn(host).empty());
}

TEST_F(WagoReplyAttributionTest, ADuplicatedVersionReplyIsNeverReadAsABallastState)
{
    const std::string host = hostFor("72");
    Calaos::IOBase *dali = makeDali(host, "t3164_dup", "7", "0");
    ASSERT_NE(nullptr, dali);

    //This PLC answers in time; the duplicate is what arrives late.
    datagram(host, VERSION_REPLY_30);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size());
    ASSERT_EQ("WAGO_DALI_GET 1 7 0", queued[0]);
    ASSERT_EQ("0", dali->get_value_string());

    datagram(host, VERSION_REPLY_30);

    EXPECT_EQ("0", dali->get_value_string())
        << "the duplicated version answer was attributed to the DALI read";
    const std::vector<std::string> still = framesOn(host);
    ASSERT_EQ(1u, still.size());
    EXPECT_EQ("WAGO_DALI_GET 1 7 0", still[0]);

    datagram(host, DALI_REPLY_ON);
    EXPECT_EQ("100", dali->get_value_string())
        << "the genuine DALI answer did not reach the ballast either";
}

/* The other side of the DALI branch, so "it became 100" above cannot be
 * satisfied by a callback that assigns 100 whatever it is handed.
 *
 * Two ballasts on one PLC, and the LIT one is read FIRST: "0" is the startup
 * value of the second, so without that half the case would be green with the
 * conversion unplugged. The queue is serial and drained in construction
 * order, which is what puts each answer on its own ballast. */
TEST_F(WagoReplyAttributionTest, AGenuineOffReplyStillLeavesTheBallastOff)
{
    const std::string host = hostFor("73");
    Calaos::IOBase *lit = makeDali(host, "t3164_off_lit", "7", "0");
    Calaos::IOBase *off = makeDali(host, "t3164_off_dark", "9", "0");
    ASSERT_NE(nullptr, lit);
    ASSERT_NE(nullptr, off);

    datagram(host, VERSION_REPLY_30);
    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(2u, queued.size());
    ASSERT_EQ("WAGO_DALI_GET 1 7 0", queued[0]);
    ASSERT_EQ("WAGO_DALI_GET 1 9 0", queued[1]);
    ASSERT_EQ("0", lit->get_value_string());
    ASSERT_EQ("0", off->get_value_string());

    datagram(host, DALI_REPLY_ON);
    ASSERT_EQ("100", lit->get_value_string())
        << "the conversion never ran, so the expectation below would hold "
           "whatever the reply said";

    datagram(host, DALI_REPLY_OFF);
    EXPECT_EQ("0", off->get_value_string())
        << "a reply whose second token is 0 must leave the ballast off";
    EXPECT_EQ("100", lit->get_value_string())
        << "the second answer was attributed to the first ballast";
    EXPECT_TRUE(framesOn(host).empty())
        << "a matching answer must consume its command";
}

/*----------------------------------------------------------------------------
 * THE GUARD IS NOT A GUARD THAT DROPS EVERYTHING.
 *--------------------------------------------------------------------------*/

/* Without this case, a guard refusing every datagram would make the two cases
 * above green, and the server would never read a single ballast. */
TEST_F(WagoReplyAttributionTest, AVersionReplyStillAnswersTheVersionQuery)
{
    const std::string host = hostFor("74");
    ASSERT_NE(nullptr, makeDali(host, "t3164_settle", "7", "1"));

    const std::vector<std::string> before = framesOn(host);
    ASSERT_EQ(1u, before.size()) << "the map did not queue its version query";
    ASSERT_EQ("WAGO_GET_VERSION", before[0]);

    datagram(host, VERSION_REPLY_30);

    const std::vector<std::string> after = framesOn(host);
    ASSERT_EQ(1u, after.size())
        << "the version answer was refused, so the deferred DALI read never "
           "left and the start barrier can never balance";

    /* And the arbitration is intact: 3.0 carries the flag, in third position.
     * Position, not presence - line is 1 and the flag is 0 or 1, so "the frame
     * contains a 1" is true before any of this. */
    const std::vector<std::string> f = fields(after[0]);
    ASSERT_EQ(4u, f.size()) << "3.0 must be asked with three parameters";
    EXPECT_EQ("WAGO_DALI_GET", f[0]);
    EXPECT_EQ("1", f[1]);
    EXPECT_EQ("7", f[2]) << "the address left its second position";
    EXPECT_EQ("1", f[3]) << "the group flag is not in third position";
}

/* THE BOUNDARY OF THE GUARD, measured. A datagram naming another command is
 * still read as a version when the VERSION QUERY is what waits: that path ends
 * on "unknown", which is the read every program has always been sent, and the
 * neighbouring suite pins it. Widening the guard to cover it would change a
 * behaviour that harms nobody, so it is left alone - and a case is owed to
 * whoever reads the guard and wonders why it is one-sided. */
TEST_F(WagoReplyAttributionTest, AForeignDatagramStillAnswersTheVersionQueryAsUnknown)
{
    const std::string host = hostFor("83");
    ASSERT_NE(nullptr, makeDali(host, "t3164_boundary", "7", "1"));

    const std::vector<std::string> before = framesOn(host);
    ASSERT_EQ(1u, before.size());
    ASSERT_EQ("WAGO_GET_VERSION", before[0]);

    datagram(host, "WAGO_HEARTBEAT 1");

    const std::vector<std::string> after = framesOn(host);
    ASSERT_EQ(1u, after.size())
        << "the deferred DALI read did not leave, so nothing gives its "
           "StartReadRules count back until the 2s timeout";
    EXPECT_EQ("WAGO_DALI_GET 1 7", after[0])
        << "a heartbeat echo was read as a usable version";
}

/* A datagram naming no command at all is not placeable, and keeps the conduct
 * it has always had. This is what bounds the guard: it refuses a MISMATCH, it
 * does not require a match. */
TEST_F(WagoReplyAttributionTest, ADatagramNamingNoCommandIsStillAttributedToTheHead)
{
    const std::string host = hostFor("75");
    Calaos::IOBase *dali = makeDali(host, "t3164_unnamed", "7", "0");
    ASSERT_NE(nullptr, dali);

    datagram(host, VERSION_REPLY_30);
    ASSERT_EQ(1u, framesOn(host).size());
    ASSERT_EQ("0", dali->get_value_string());

    datagram(host, "REPLY 1 77");

    EXPECT_EQ("100", dali->get_value_string())
        << "an unnamed datagram used to be attributed to the head, and this "
           "fix does not change that";
    EXPECT_TRUE(framesOn(host).empty());
}

/*----------------------------------------------------------------------------
 * THE ARBITRATION OF THE VERSION QUERY, UNMOVED BY A DROPPED DATAGRAM.
 *--------------------------------------------------------------------------*/

/* An unknown version keeps the two-parameter form, and a version answer that
 * arrives too late must not promote it: the frames already sent were built on
 * "unknown", and a later promotion would split one PLC's reads in two. */
TEST_F(WagoReplyAttributionTest, ALateVersionReplyDoesNotPromoteAnUnknownPlc)
{
    const std::string host = hostFor("76");
    ASSERT_NE(nullptr, makeDali(host, "t3164_unknown_a", "7", "1"));
    timeoutOnce(host);

    const std::vector<std::string> first = framesOn(host);
    ASSERT_EQ(1u, first.size());
    ASSERT_EQ("WAGO_DALI_GET 1 7", first[0])
        << "an unknown version must keep the two parameter read";

    datagram(host, VERSION_REPLY_30);
    datagram(host, DALI_REPLY_ON);

    //A second ballast on the same PLC, built after the late datagram.
    ASSERT_NE(nullptr, makeDali(host, "t3164_unknown_b", "9", "1"));
    const std::vector<std::string> second = framesOn(host);
    ASSERT_EQ(1u, second.size());
    EXPECT_EQ("WAGO_DALI_GET 1 9", second[0])
        << "the late version answer promoted the PLC to 3.0 and the second "
           "read now carries a flag the first one did not";
}

/* The under-3.0 half of the same arbitration, so "two parameters" above is not
 * satisfied by a server that never appends anything. */
TEST_F(WagoReplyAttributionTest, APlcAnnouncingTwoThreeKeepsTheTwoParameterRead)
{
    const std::string host = hostFor("77");
    ASSERT_NE(nullptr, makeDali(host, "t3164_v23", "7", "1"));

    datagram(host, VERSION_REPLY_23);

    const std::vector<std::string> queued = framesOn(host);
    ASSERT_EQ(1u, queued.size());
    const std::vector<std::string> f = fields(queued[0]);
    ASSERT_EQ(3u, f.size())
        << "a 2.3 PLC was sent a third parameter, which it reads as a DMX "
           "address: '" << queued[0] << "'";
    EXPECT_EQ("7", f[2]) << "the address left its second position";
}

/*----------------------------------------------------------------------------
 * THE START BARRIER.
 *--------------------------------------------------------------------------*/

/* THE SENSOR FIRST. A counter that never moved would make every barrier case
 * below green for nothing. */
TEST_F(WagoReplyAttributionTest, TheBarrierCounterMovesAndComesBack)
{
    const std::string host = hostFor("78");
    const int before = barrier();

    ASSERT_NE(nullptr, makeDali(host, "t3164_sensor", "7", "0"));
    ASSERT_EQ(before + 1, barrier())
        << "building a WODali did not count anything into the barrier";

    datagram(host, VERSION_REPLY_30);
    EXPECT_EQ(before + 1, barrier())
        << "the version answer gave the count back on its own, so the barrier "
           "no longer waits for the ballast at all";

    datagram(host, DALI_REPLY_ON);
    EXPECT_EQ(before, barrier());
}

TEST_F(WagoReplyAttributionTest, TheBarrierBalancesAfterALateVersionReply)
{
    const std::string host = hostFor("79");
    const int before = barrier();

    ASSERT_NE(nullptr, makeDali(host, "t3164_bal_late", "7", "0"));
    ASSERT_EQ(before + 1, barrier());

    timeoutOnce(host);
    ASSERT_EQ(before + 1, barrier())
        << "the expired version query gave back a count that belongs to the "
           "ballast";

    datagram(host, VERSION_REPLY_30);
    EXPECT_EQ(before + 1, barrier())
        << "the dropped datagram gave the ballast's count back without the "
           "ballast ever being read";

    //What the head still owes is given back by its own 2s timeout.
    timeoutOnce(host);
    EXPECT_EQ(before, barrier())
        << "the DALI read owes a count that neither a reply nor its timeout "
           "gives back: the server would never run its start rules";
}

TEST_F(WagoReplyAttributionTest, TheBarrierBalancesAfterADuplicatedVersionReply)
{
    const std::string host = hostFor("80");
    const int before = barrier();

    ASSERT_NE(nullptr, makeDali(host, "t3164_bal_dup", "7", "0"));
    ASSERT_EQ(before + 1, barrier());

    datagram(host, VERSION_REPLY_30);
    ASSERT_EQ(before + 1, barrier());

    datagram(host, VERSION_REPLY_30);
    EXPECT_EQ(before + 1, barrier());

    timeoutOnce(host);
    EXPECT_EQ(before, barrier());
}

/* Three channels, three deferred reads, three counts - and a dropped datagram
 * in the middle of them. */
TEST_F(WagoReplyAttributionTest, TheBarrierBalancesForAllThreeRVBChannelsAfterADrop)
{
    const std::string host = hostFor("81");
    const int before = barrier();

    ASSERT_NE(nullptr, makeDaliRVB(host, "t3164_rvb"));
    ASSERT_EQ(before + 3, barrier());

    timeoutOnce(host);
    ASSERT_EQ(3u, framesOn(host).size())
        << "the three deferred channel reads did not all leave";

    datagram(host, VERSION_REPLY_30);
    EXPECT_EQ(before + 3, barrier());
    EXPECT_EQ(3u, framesOn(host).size())
        << "the dropped datagram consumed one of the three channel reads";

    for (int i = 0; i < 3; i++)
        timeoutOnce(host);
    EXPECT_EQ(before, barrier());
    EXPECT_TRUE(framesOn(host).empty());
}

/* THE DROP MUST NOT CANCEL THE TIMEOUT IT RELIES ON. Dropping a datagram
 * leaves the head owing its callback; the only thing that still gives it back
 * is the 2s timer. A guard placed after the timer is deleted would take that
 * away, and the count would be owed for ever with no other symptom. */
TEST_F(WagoReplyAttributionTest, TheDroppedDatagramLeavesTheHeadTimeoutArmed)
{
    const std::string host = hostFor("82");
    ASSERT_NE(nullptr, makeDali(host, "t3164_timer", "7", "0"));

    //Arming is done by the production send step, never by this fixture.
    WagoMapProbe::sendHead(mapOn(host));
    ASSERT_TRUE(WagoMapProbe::timeoutArmed(mapOn(host)))
        << "the version query was sent without arming its timeout: this case "
           "measures nothing";

    datagram(host, VERSION_REPLY_30);
    ASSERT_FALSE(WagoMapProbe::timeoutArmed(mapOn(host)))
        << "a matching answer must disarm the timeout it satisfies";

    WagoMapProbe::sendHead(mapOn(host));
    ASSERT_TRUE(WagoMapProbe::timeoutArmed(mapOn(host)));

    datagram(host, VERSION_REPLY_30);
    EXPECT_TRUE(WagoMapProbe::timeoutArmed(mapOn(host)))
        << "the dropped datagram disarmed the head's timeout, so nothing will "
           "ever give its StartReadRules count back";
}
