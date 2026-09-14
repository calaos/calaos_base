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
 * A READ THE PLC REFUSED, READ AS A LIT LUMINAIRE.
 *
 * "WAGO_DALI_GET ERR <reason>" is how a program from 4.0 on answers a DALI
 * read it cannot serve. The verb is kept because udpReplyMisattributedToDaliGet
 * (WagoMap.cpp:49-58) discards any other WAGO_ verb while a read is in flight,
 * so the refusal arrives at the caller looking exactly like an answer.
 *
 * Two readers then take it apart:
 *   WODali       reads tokens[1] and spells "lit" as "not the string 0", so
 *                ERR became value 100 / old_value 0, emitChange() published
 *                "lit, 100%", and ioRead() gave the start-up token back as if
 *                the read had succeeded.
 *   WODaliRVB    reads tokens[2] - the REASON - through from_string(), which
 *                publishes 0 for a token it cannot read. The -1 that means
 *                "this channel has not answered yet" was destroyed and
 *                checkReadState() published a colour for a channel nobody
 *                ever answered.
 * Third pass over the same edge after -1 and 0: no reply may ever encode "I
 * do not know" inside the value domain.
 *
 * WHAT A CASE HERE HAS TO PROVE, AND THE TWO WAYS IT COULD LIE.
 *
 * 1. THE FRAME MUST HAVE ARRIVED. If the refusal were dropped upstream - by
 *    the attribution filter, or by WODali's `tokens.size() >= 3` guard -
 *    nothing would reach the comparison and a green case would measure
 *    nothing. Every case below therefore reads WagoMap's pending queue and
 *    requires the frame to have been POPPED, and
 *    AnErrorReplyIsDeliveredAndNotDroppedOnTheWay pairs that reading with a
 *    frame that really is dropped, so "the queue emptied" is a measurement
 *    and not a reading of this comment.
 *
 * 2. `value` STARTS AT 0, so "the ballast is off" is also what an IO that was
 *    never served says. Every case pairs the refusal with a ballast that got
 *    a real answer on the same fixture and reached 100, and counts the
 *    StartReadRules tokens: exactly one comes back, not zero and not two.
 *
 * WHY THE ADDRESS IS 77 AND THE LEVEL 77. Neither is 0 or 1, so no assertion
 * here can be satisfied by a frame whose fields were permuted, and no level
 * can be confused with a group flag or a line number.
 *
 * ⛔ NO VERSION GATE, AND THE SUITE PINS THAT. ERR is a level under no
 * version of the protocol, so AnErrorReplyIsRefusedWhateverVersionWasAnnounced
 * plays 2.3, 3.0, 4.0 and a PLC that never announced anything. A gate on the
 * announced version reddens on the first three of those.
 *
 * One host per case, TEST-NET-1 (RFC 5737): syntactically valid, never local,
 * so bind() fails with EADDRNOTAVAIL and this binary cannot collide with a
 * parallel `make check -j` peer over WAGO_LISTEN_PORT. A fresh host is also a
 * fresh WagoMap singleton. Same reasoning as core/WagoUdpReply_test and
 * core/WagoDaliGetGroup_test.
 *
 * ⛔ WHAT IS NOT MEASURED HERE, declared and not promised:
 *   - No PLC is reached and none can be: the calaos_wago programs are CoDeSys
 *     binaries and nothing in this tree can run one. That a 4.0 program really
 *     answers this frame, on this bus, is the specification's word. Every case
 *     injects the frame itself.
 *   - GROUP_UNSUPPORTED is the only reason anyone has named. Nothing bounds
 *     the ones to come, and no case can enumerate them.
 *   - A refusal carrying NO reason ("WAGO_DALI_GET ERR", two fields) is
 *     refused by the production code, but no oracle in this tree separates
 *     that from the old behaviour: the old `>= 3` guard already left the
 *     ballast alone on a two-field frame. The case is therefore absent rather
 *     than green for the wrong reason.
 *   - WODaliRVB::WagoUDPCommand_cb (WODaliRVB.cpp) reads nothing on success
 *     and is not a state read; it is untouched and untested here.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"
#include "IOFactory.h"
#include "WagoMap.h"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

/* The refusal, and the three frames that must NOT be taken for one. They all
 * carry the same number of fields as the refusal except where noted, so no
 * assertion below can be satisfied by counting fields. */
const char *const DALI_REFUSED = "WAGO_DALI_GET ERR GROUP_UNSUPPORTED";
const char *const DALI_LIT = "WAGO_DALI_GET 1 77";
const char *const DALI_ERR_IN_THE_REASON = "WAGO_DALI_GET 1 ERR";
const char *const DALI_UNREADABLE_LEVEL = "WAGO_DALI_GET XYZ GROUP_UNSUPPORTED";

/* A reply naming another command, which udpReplyMisattributedToDaliGet really
 * does throw away. It is the yardstick for "the frame arrived". */
const char *const DALI_MISATTRIBUTED = "WAGO_GET_VERSION 4.0 750-849";

/* The production queue of frames waiting for the wire, and the production
 * timeout entry point, both reached through a pointer-to-member formed on a
 * derived class. Nothing is ever constructed from this type. */
struct WagoMapProbe: Calaos::WagoMap
{
    static size_t pending(const Calaos::WagoMap &map)
    {
        auto queued = map.*(&WagoMapProbe::udp_commands);
        return queued.size();
    }

    static void fireTimeout(Calaos::WagoMap &map)
    {
        (map.*(&WagoMapProbe::UDPCommandTimeout_cb))();
    }
};

} //namespace

class WagoDaliErrorReplyTest: public CalaosTest::CoreFixture
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

    size_t pendingOn(const std::string &host) const
    {
        return WagoMapProbe::pending(mapOn(host));
    }

    int barrier() const { return Calaos::StartReadRules::Instance().pendingIOCount(); }

    //The WagoMap constructor asks WAGO_GET_VERSION first and holds every DALI
    //read until that answer lands, so the version query is what sits at the
    //head of the queue. It has to go by before any case can reach a ballast.
    void settleVersion(const std::string &host, int major, int minor) const
    {
        mapOn(host).udpRequest_cb(true, "WAGO_GET_VERSION " + std::to_string(major) +
                                        "." + std::to_string(minor) + " 750-849");
    }

    //A PLC that never announced anything: the 2s timer fires on the version
    //query, through the very function it calls in production.
    void settleVersionBySilence(const std::string &host) const
    {
        WagoMapProbe::fireTimeout(mapOn(host));
    }

    void answer(const std::string &host, const char *reply) const
    {
        mapOn(host).udpRequest_cb(true, reply);
    }

    Calaos::IOBase *makeDali(const std::string &host, const std::string &id,
                             const std::string &address = "77")
    {
        Params p = {{ "type", "WODali" },
                    { "id", id },
                    { "name", "dali ballast" },
                    { "host", host },
                    { "line", "1" },
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
                    { "name", "dali rgb ballast" },
                    { "host", host },
                    { "rline", "1" }, { "raddress", "71" }, { "rgroup", "0" },
                    { "rfade_time", "1" },
                    { "gline", "1" }, { "gaddress", "72" }, { "ggroup", "0" },
                    { "gfade_time", "1" },
                    { "bline", "1" }, { "baddress", "73" }, { "bgroup", "0" },
                    { "bfade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    //A ballast that has settled its version and is waiting for its one read.
    Calaos::IOBase *armedDali(const std::string &suffix, const std::string &id)
    {
        const std::string host = hostFor(suffix);
        Calaos::IOBase *dali = makeDali(host, id);
        EXPECT_NE(nullptr, dali);
        settleVersion(host, 4, 0);
        EXPECT_EQ(1u, pendingOn(host))
            << "the ballast on " << host << " is not waiting for a read: the "
               "case that follows would measure nothing";
        return dali;
    }
};

/*----------------------------------------------------------------------------
 * ⭐ FIRST, THAT THE REFUSAL REACHES THE READER AT ALL.
 *
 * Everything else in this file reads as "the ballast did not move", which is
 * also what an IO that was never handed anything says. This case is what
 * separates the two, and it carries its own yardstick: a frame the attribution
 * filter really does discard leaves the read QUEUED, so "the queue emptied" is
 * a property of delivery and not of calling udpRequest_cb().
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, AnErrorReplyIsDeliveredAndNotDroppedOnTheWay)
{
    const std::string dropped = hostFor("11");
    ASSERT_NE(nullptr, makeDali(dropped, "dali_dropped"));
    settleVersion(dropped, 4, 0);
    ASSERT_EQ(1u, pendingOn(dropped));

    answer(dropped, DALI_MISATTRIBUTED);
    ASSERT_EQ(1u, pendingOn(dropped))
        << "udpReplyMisattributedToDaliGet no longer discards a reply naming "
           "another command - the emptiness read below proves nothing";

    const std::string refused = hostFor("12");
    ASSERT_NE(nullptr, makeDali(refused, "dali_refused_delivery"));
    settleVersion(refused, 4, 0);
    ASSERT_EQ(1u, pendingOn(refused));

    answer(refused, DALI_REFUSED);
    EXPECT_EQ(0u, pendingOn(refused))
        << "'" << DALI_REFUSED << "' never reached the ballast: it was "
           "discarded while a DALI read was in flight, so nothing in this "
           "file measures what the reader does with it";
}

/*----------------------------------------------------------------------------
 * ⭐⭐ THE DEFECT: the refusal must leave the ballast where a timeout does.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, AnErrorReplyLeavesTheBallastWhereATimeoutLeavesIt)
{
    Calaos::IOBase *refused = armedDali("13", "dali_refused");
    Calaos::IOBase *silent = armedDali("14", "dali_silent");
    Calaos::IOBase *lit = armedDali("15", "dali_lit");

    answer(hostFor("13"), DALI_REFUSED);
    WagoMapProbe::fireTimeout(mapOn(hostFor("14")));
    answer(hostFor("15"), DALI_LIT);

    //The control, and it is what gives the two lines below their meaning: a
    //real answer on this very fixture DOES move a ballast to 100.
    ASSERT_EQ("100", lit->get_value_string())
        << "no reply moves a ballast in this fixture - every expectation in "
           "this case is passing for free";

    EXPECT_EQ(silent->get_value_string(), refused->get_value_string())
        << "a refused read published '" << refused->get_value_string()
        << "' where a silent PLC publishes '" << silent->get_value_string()
        << "': ERR was read as a level, and the luminaire shows lit";
    EXPECT_EQ(silent->get_value_bool(), refused->get_value_bool());
    EXPECT_NE(lit->get_value_string(), refused->get_value_string());

    EXPECT_EQ(0u, pendingOn(hostFor("13")))
        << "the refusal was not delivered, so the equality above says nothing";
}

/*----------------------------------------------------------------------------
 * The start-up barrier. A read that never gives its token back leaves the
 * server without start rules for good, and a read that gives it back twice
 * releases the barrier before the other IOs have answered.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, AnErrorReplyGivesBackExactlyOneStartReadToken)
{
    armedDali("16", "dali_token_refused");
    const int beforeRefused = barrier();
    answer(hostFor("16"), DALI_REFUSED);
    const int afterRefused = barrier();

    //The control: a reply that IS served gives back exactly one token too, so
    //"exactly one" is a property of the path and not of this arithmetic.
    armedDali("17", "dali_token_lit");
    const int beforeLit = barrier();
    answer(hostFor("17"), DALI_LIT);
    const int afterLit = barrier();

    ASSERT_EQ(1, beforeLit - afterLit)
        << "a served DALI read no longer gives back exactly one token - the "
           "expectation below is measured against a broken yardstick";

    EXPECT_EQ(1, beforeRefused - afterRefused)
        << "a refused DALI read gave back " << (beforeRefused - afterRefused)
        << " start-read tokens instead of one";
}

/*----------------------------------------------------------------------------
 * ⭐ POSITION, NOT PRESENCE. ERR is a refusal in the field the LEVEL is read
 * from and nowhere else; anywhere else it is just a field of a reply that was
 * served. A reader that searched the frame for the word would take a reason
 * that happens to be spelled ERR for a refusal.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, TheRefusalIsReadAtTheLevelPositionAndNowhereElse)
{
    Calaos::IOBase *atLevel = armedDali("18", "dali_err_at_level");
    Calaos::IOBase *atReason = armedDali("19", "dali_err_at_reason");

    answer(hostFor("18"), DALI_REFUSED);
    answer(hostFor("19"), DALI_ERR_IN_THE_REASON);

    EXPECT_EQ("0", atLevel->get_value_string())
        << "'" << DALI_REFUSED << "' was read as a level";
    EXPECT_EQ("100", atReason->get_value_string())
        << "'" << DALI_ERR_IN_THE_REASON << "' was taken for a refusal: the "
           "word was looked for in the frame instead of at the position the "
           "level is read from";
}

/*----------------------------------------------------------------------------
 * ⭐ THE VALUE, NOT THE SHAPE. Both frames here have three fields and an
 * unreadable one in second position; only one of them says ERR. A guard that
 * keyed on the field count, or on "this is not a number", would treat them
 * alike - and would also start swallowing every reply a 2.3 program sends.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, OnlyTheWordErrIsARefusal)
{
    Calaos::IOBase *refused = armedDali("20", "dali_word_err");
    Calaos::IOBase *other = armedDali("21", "dali_word_other");

    answer(hostFor("20"), DALI_REFUSED);
    answer(hostFor("21"), DALI_UNREADABLE_LEVEL);

    EXPECT_EQ("0", refused->get_value_string());
    EXPECT_EQ("100", other->get_value_string())
        << "'" << DALI_UNREADABLE_LEVEL << "' has the shape of the refusal and "
           "not its value, and it was refused anyway: the guard reads the "
           "shape of the frame instead of the field";
}

/*----------------------------------------------------------------------------
 * ⭐⭐ NO VERSION GATE. ERR belongs to the value domain of no version, so the
 * refusal is honoured whatever the PLC announced - including a PLC that
 * announced nothing, which is every installation whose version datagram was
 * lost. A gate at 4.0 reddens on the first three rows here.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, AnErrorReplyIsRefusedWhateverVersionWasAnnounced)
{
    struct { const char *suffix; int major, minor; bool announced; } cases[] = {
        { "22", 2, 3, true },
        { "23", 3, 0, true },
        { "24", 4, 0, true },
        { "25", 0, 0, false },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        const std::string host = hostFor(cases[i].suffix);
        Calaos::IOBase *dali = makeDali(host, std::string("dali_v") + cases[i].suffix);
        ASSERT_NE(nullptr, dali);

        if (cases[i].announced)
            settleVersion(host, cases[i].major, cases[i].minor);
        else
            settleVersionBySilence(host);

        ASSERT_EQ(1u, pendingOn(host)) << "host " << host;

        const int before = barrier();
        answer(host, DALI_REFUSED);

        EXPECT_EQ(0u, pendingOn(host)) << "host " << host;
        EXPECT_EQ("0", dali->get_value_string())
            << "a PLC announcing "
            << (cases[i].announced ? std::to_string(cases[i].major) + "." +
                                     std::to_string(cases[i].minor)
                                   : std::string("nothing"))
            << " had its refusal read as a level: the refusal is gated on a "
               "version, and ERR is a level under none of them";
        EXPECT_EQ(1, before - barrier()) << "host " << host;
    }
}

/*----------------------------------------------------------------------------
 * ⭐ THE SECOND READER. Each WODaliRVB channel reads the REASON as its level
 * through from_string(), which publishes 0 for a token it cannot read - so a
 * refusal used to destroy the -1 that means "not answered yet" and let
 * checkReadState() publish a colour for a channel nobody answered.
 *
 * The oracle is get_command_string(), which OutputLightRGB writes ONLY when
 * something publishes (OutputLightRGB.cpp:330). It starts empty, so "still
 * empty" is "nothing was published" and not "published something that looks
 * like the start-up value" - which is exactly what get_value_string() would
 * have said here, since a colour with one dead channel publishes state false
 * and reads back as "0", the same text as a light that was never read.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliErrorReplyTest, ARefusedChannelPublishesNoColour)
{
    const std::string refused = hostFor("26");
    const std::string served = hostFor("27");
    Calaos::IOBase *rgbRefused = makeDaliRVB(refused, "dali_rgb_refused");
    Calaos::IOBase *rgbServed = makeDaliRVB(served, "dali_rgb_served");
    ASSERT_NE(nullptr, rgbRefused);
    ASSERT_NE(nullptr, rgbServed);

    settleVersion(refused, 4, 0);
    settleVersion(served, 4, 0);
    ASSERT_EQ(3u, pendingOn(refused));
    ASSERT_EQ(3u, pendingOn(served));
    ASSERT_EQ("", rgbRefused->get_command_string())
        << "the light published something before any reply arrived - the "
           "oracle of this case is not empty to begin with";

    const int beforeRefused = barrier();
    //Red is refused, green and blue answer. The queued reads are answered in
    //the order they were queued.
    answer(refused, DALI_REFUSED);
    answer(refused, DALI_LIT);
    answer(refused, DALI_LIT);
    const int afterRefused = barrier();

    //The control: the same three reads, all served.
    answer(served, DALI_LIT);
    answer(served, DALI_LIT);
    answer(served, DALI_LIT);

    ASSERT_EQ(0u, pendingOn(refused))
        << "the three replies did not reach the light";
    ASSERT_NE("", rgbServed->get_command_string())
        << "three served reads published nothing either - the expectation "
           "below is passing for free";

    EXPECT_EQ("", rgbRefused->get_command_string())
        << "a light with ONE refused channel published '"
        << rgbRefused->get_command_string() << "': the reason field was "
           "converted into a level, and the channel that was never read "
           "counted as read";
    EXPECT_EQ(3, beforeRefused - afterRefused)
        << "the three channels gave back " << (beforeRefused - afterRefused)
        << " start-read tokens instead of three";
}
