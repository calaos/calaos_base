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
 * A SHUTTER HAS NO RESYNCHRONISATION POINT WITH THE PLC. CHARACTERISATION.
 *
 * THIS IS NOT A REGRESSION SUITE. It pins what the tree does TODAY, so
 * that the day the arbitration below is settled the change is visible instead
 * of silent. Three of its cases are EXPECTED TO TURN RED when a resync is
 * added; each says so in its own body.
 *
 * The defect it characterises. `netOutStandard` lives in the PLC's memory and
 * a server reboot does not clear it. When the heartbeat comes back the PLC
 * republishes the output image of just before the outage, relay bits and all.
 * A shutter occupies TWO relay bits; if the server died mid travel one of them
 * was set, so it is set again and the shutter leaves on its own.
 *
 * `WODigital` has a resynchronisation point - it subscribes to
 * `WagoMap::onWagoConnected` and reads its own output bit back
 * (WODigital.cpp:62-67, `read_output_bits`). `WOVoletBase::voletInit()`
 * (WagoIOBase.h:224-230) has none: it parses parameters, touches the WagoMap
 * singleton into existence, and reads and writes NOTHING. Both `WOVolet` and
 * `WOVoletSmart` go through it, so both are in the same state.
 *
 * WHAT IS OBSERVED, AND WHY IT IS THE RELAYS AND NOT A LOG.
 * The only thing the server can do to a relay is put a Modbus command on the
 * wire for it. `WagoMap` keeps every command it has issued and not yet seen
 * answered in a protected `mbus_commands`; naming that member through a
 * derived class yields a pointer-to-member of the BASE, which reads the real
 * production table without adding a line to production. Nothing here mocks a
 * relay: what is measured is whether a relay command exists at all.
 *
 * THE EXPECTED VALUE OF THE THREE SUBJECT CASES IS THE EMPTY SET, which is
 * exactly the shape a dead probe also returns. Three controls stand against
 * that, and they are the reason this file can claim anything:
 *
 *   - ADigitalThatSeesThePlcComeBackReadsItsOwnBitBack - the SAME probe, the
 *     same emission, on the class that does have a resync point. It must see
 *     one read_output_bits.
 *   - AVoletAddsNothingToWhatADigitalOnItsOwnHostAsks - the digital and the
 *     shutter on ONE host. The probe is demonstrably listening to that host,
 *     and the shutter still contributes nothing. Without it, "empty" could be
 *     an artefact of looking at the wrong WagoMap.
 *   - TheVoletsOwnRelayWritesDoReachThisProbe - the sentinel. The SAME
 *     shutter object, driven by the production `up` action, must show up as
 *     two relay writes. A probe blind to WOVolet in particular would pass the
 *     two controls above and fail this one.
 *
 * WHY THE COMMAND CODES ARE SPELLED OUT AS NUMBERS. The oracle must not be
 * read from the same enumeration the production code writes with, or renaming
 * or reordering `MBUS_*` would move the expectation along with the behaviour.
 * The decoder below uses literals, and one case pins those literals against
 * the production enumeration in both directions.
 *
 * WHAT THIS FILE DOES NOT CLOSE, declared and not promised:
 *   - Nothing here reaches a PLC. That the republished image really re-excites
 *     the relay, and how long the PLC takes to switch back to server mode,
 *     need the hardware.
 *   - `ExternProcServer::sendMessage()` drops its payload when no sidecar is
 *     connected, which is the case in this process. The ADDRESS and the VALUE
 *     of each command are therefore not observable here - only its kind and
 *     the fact that it was issued. A suite that wanted to tell the up relay
 *     from the down relay would need the sidecar.
 *   - The half of the defect that lives in the PLC program is untouched: the
 *     server cannot keep the image from being republished, only overwrite it
 *     afterwards.
 *--------------------------------------------------------------------------*/

#include "CalaosCoreFixture.h"

#include "IOFactory.h"
#include "WagoMap.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* The Modbus command codes this file asserts on, written as literals on
 * purpose - see the header. TheModbusCommandCodesThisFileNamesAreThePlcOnes
 * ties them back to the production enumeration. */
const int CODE_READ_OUTPUT_BITS = 2;
const int CODE_WRITE_BIT = 3;

std::string commandName(int code)
{
    if (code == CODE_READ_OUTPUT_BITS) return "read_output_bits";
    if (code == CODE_WRITE_BIT) return "write_bit";
    return "other(" + Utils::to_string(code) + ")";
}

/* The production table of Modbus commands issued and not yet answered, read
 * through a pointer-to-member formed on a derived class. Nothing is ever
 * constructed from this type. */
struct WagoMapProbe: Calaos::WagoMap
{
    static std::vector<int> codes(const Calaos::WagoMap &map)
    {
        std::vector<int> out;
        for (const auto &entry: map.*(&WagoMapProbe::mbus_commands))
            out.push_back(entry.second.command);
        //unordered_map, so the emission order is not observable; the SET is.
        std::sort(out.begin(), out.end());
        return out;
    }
};

/* Every Modbus command that host has been handed, by name, sorted. */
std::vector<std::string> relayTrafficFor(const std::string &host)
{
    std::vector<std::string> out;
    for (WagoMap *m: WagoMap::get_maps())
    {
        if (m->get_host() != host) continue;
        for (int code: WagoMapProbe::codes(*m))
            out.push_back(commandName(code));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string describe(const std::vector<std::string> &traffic)
{
    std::string s = "{";
    for (size_t i = 0; i < traffic.size(); i++)
    {
        if (i) s += ", ";
        s += traffic[i];
    }
    return s + "}";
}

/* The signal a reconnecting sidecar fires (WagoMap.cpp:83). Firing the
 * production signal is what makes this a test of the reconnection path and
 * not a story about it. */
void plcComesBack(const std::string &host)
{
    for (WagoMap *m: WagoMap::get_maps())
    {
        if (m->get_host() == host)
            m->onWagoConnected.emit();
    }
}

}

class WagoVoletRestartTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    /* One host per case: a fresh host is a fresh WagoMap singleton, hence a
     * command table holding this case's traffic and nothing else. TEST-NET-1
     * (RFC 5737) is syntactically valid and never local, so bind() fails with
     * EADDRNOTAVAIL and this binary can never collide with a parallel
     * `make check -j` peer over WAGO_LISTEN_PORT. Same reasoning as
     * core/WagoPortDefault_test. */
    std::string hostFor(const std::string &suffix) const
    {
        return "192.0.2." + suffix;
    }

    IOBase *makeVolet(const std::string &type, const std::string &host,
                      const std::string &id)
    {
        Params p = {{ "type", type },
                    { "id", id },
                    { "name", "shutter under test" },
                    { "host", host },
                    { "var_up", "7" },
                    { "var_down", "11" },
                    { "time", "60" },
                    { "time_up", "60" },
                    { "time_down", "58" },
                    { "wago_841", "true" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    IOBase *makeDigital(const std::string &host, const std::string &id)
    {
        Params p = {{ "type", "WODigital" },
                    { "id", id },
                    { "name", "light under test" },
                    { "host", host },
                    { "var", "22" },
                    { "wago_841", "true" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }
};

/*******************************************************************************
 * THE CALIBRATION. The decoder above uses literals so that the oracle cannot
 * drift with the production enumeration; this is what keeps the two honest,
 * and it pins BOTH directions - the code the name maps to, and the name the
 * code maps to.
 ******************************************************************************/

TEST_F(WagoVoletRestartTest, TheModbusCommandCodesThisFileNamesAreThePlcOnes)
{
    EXPECT_EQ(CODE_READ_OUTPUT_BITS, (int)MBUS_READ_OUTBITS);
    EXPECT_EQ(CODE_WRITE_BIT, (int)MBUS_WRITE_BIT);

    EXPECT_EQ("read_output_bits", commandName((int)MBUS_READ_OUTBITS));
    EXPECT_EQ("write_bit", commandName((int)MBUS_WRITE_BIT));
    EXPECT_EQ("other(1)", commandName((int)MBUS_READ_BITS));
}

/*******************************************************************************
 * THE CONTROLS. Read these before the three subject cases: the subjects all
 * expect the empty set, and only these say that an empty set means anything.
 ******************************************************************************/

TEST_F(WagoVoletRestartTest, ADigitalThatSeesThePlcComeBackReadsItsOwnBitBack)
{
    //POSITIVE CONTROL. WODigital.cpp:62-67 is the resync point a shutter does
    //not have. If this case ever goes quiet, every empty set below measures a
    //deaf probe and not an absent command.
    const std::string host = hostFor("31");
    ASSERT_NE(nullptr, makeDigital(host, "t3161_digital_alone"));

    plcComesBack(host);

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>({ "read_output_bits" }), traffic)
            << "WODigital no longer adopts the PLC bit on reconnection: "
            << describe(traffic);
}

TEST_F(WagoVoletRestartTest, AVoletAddsNothingToWhatADigitalOnItsOwnHostAsks)
{
    //HAYSTACK CONTROL. Both IOs share one host, so the probe is listening to
    //a WagoMap that demonstrably has traffic. What the shutter adds to it is
    //the whole measurement, and it is nothing.
    //EXPECTED TO TURN RED once a shutter resync is added.
    const std::string host = hostFor("32");
    ASSERT_NE(nullptr, makeDigital(host, "t3161_digital_shared"));
    ASSERT_NE(nullptr, makeVolet("WOVolet", host, "t3161_volet_shared"));

    plcComesBack(host);

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>({ "read_output_bits" }), traffic)
            << "the shutter now speaks to the PLC on reconnection: "
            << describe(traffic);
}

TEST_F(WagoVoletRestartTest, TheVoletsOwnRelayWritesDoReachThisProbe)
{
    //SENTINEL. Same object, same probe, a non zero expectation: the
    //production `up` action drives setOutputUp()/setOutputDown() and those
    //are the two relay writes. A probe that could not see WOVolet at all
    //would still pass the two controls above and fail here.
    const std::string host = hostFor("33");
    IOBase *io = makeVolet("WOVolet", host, "t3161_volet_sentinel");
    ASSERT_NE(nullptr, io);
    ASSERT_TRUE(relayTrafficFor(host).empty());

    io->set_value(std::string("up"));

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>({ "write_bit", "write_bit" }), traffic)
            << "the shutter's own relay writes are invisible to this probe, "
            << "so nothing it reports about their absence means anything: "
            << describe(traffic);
}

/*******************************************************************************
 * THE SUBJECTS. All three expect silence, and all three are expected to turn
 * red the day a resynchronisation point is added.
 ******************************************************************************/

TEST_F(WagoVoletRestartTest, AFreshVoletAsksThePlcNothing)
{
    //voletInit() parses and touches the WagoMap singleton into existence.
    //Nothing more: no read of the two relay bits, no assertion of a rest
    //level.
    //EXPECTED TO TURN RED if the resync is put in the constructor.
    const std::string host = hostFor("34");
    ASSERT_NE(nullptr, makeVolet("WOVolet", host, "t3161_volet_fresh"));

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>(), traffic)
            << "voletInit() now talks to the PLC: " << describe(traffic);
}

TEST_F(WagoVoletRestartTest, AVoletThatSeesThePlcComeBackAsksItNothing)
{
    //The reconnection is the moment the PLC's stale image becomes the truth
    //again, and it is the moment WODigital uses. The shutter is not
    //subscribed to it at all.
    //EXPECTED TO TURN RED once a shutter resync is added.
    const std::string host = hostFor("35");
    ASSERT_NE(nullptr, makeVolet("WOVolet", host, "t3161_volet_reconnect"));

    plcComesBack(host);

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>(), traffic)
            << "the shutter now has a resync point: " << describe(traffic);
}

TEST_F(WagoVoletRestartTest, ASmartVoletThatSeesThePlcComeBackAsksItNothingEither)
{
    //The defect is not in WOVolet, it is in the WOVoletBase both shutters
    //share; measuring only one of them would let a fix land on one and leave
    //the other as it is.
    //EXPECTED TO TURN RED once a shutter resync is added to WOVoletBase.
    const std::string host = hostFor("36");
    ASSERT_NE(nullptr, makeVolet("WOVoletSmart", host, "t3161_smart_reconnect"));

    plcComesBack(host);

    const std::vector<std::string> traffic = relayTrafficFor(host);
    EXPECT_EQ(std::vector<std::string>(), traffic)
            << "the smart shutter now has a resync point: " << describe(traffic);
}
