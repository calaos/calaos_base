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
 * WHAT THE DMX ADDRESS BOUND OF WODali/WODaliRVB ACTUALLY DOES.
 *
 * The bound handed to ioDoc->paramAddInt("address", ..., 1, 355, true) is
 * DOCUMENTATION AND NOTHING ELSE. IODoc only ever renders it (genDocJson,
 * genDocMd); no path in calaos_server compares an io.xml value against it.
 *
 * ⛔ THE ONE THING THIS FILE MUST NOT DO is assert that the bound is 355.
 * That number lives in one literal, a case would read it back from the JSON
 * the same literal produced, and it would be green the day the bound is wrong
 * exactly as it is green today - the sensor that measures nothing.
 *
 * What is measurable, and what these cases are for: an address OUTSIDE the
 * documented range still loads and still reaches the wire unchanged. That is
 * the answer to "does tightening the ceiling break an installation that
 * already declares 400?" - it does not, because nothing enforces the ceiling
 * at load. The day someone adds enforcement, these cases redden and the
 * decision is taken deliberately instead of silently.
 *
 * And the second half: calaos_base NEVER converts an address into a DMX
 * channel. The string of the parameter is copied into the frame and all the
 * `address - DMX_ADDR_BASE` arithmetic is on the PLC side. That is why the
 * base being exclusive (channel 1 is address 101, 100 is not a DMX address)
 * changes a sentence and no behaviour here.
 *
 * One host per case, TEST-NET-1 (RFC 5737): syntactically valid, never local,
 * so bind() fails with EADDRNOTAVAIL and this binary cannot collide with a
 * parallel `make check -j` peer over WAGO_LISTEN_PORT. A fresh host is also a
 * fresh WagoMap singleton, holding this case's frames and nothing else.
 *
 * ⛔ NOT MEASURED HERE: what a 4.0 program does with an address past its own
 * array. The calaos_wago programs are CoDeSys binaries and nothing in this
 * tree can run one; DMX_CHANNELS = 255, and therefore the ceiling of 355, is
 * the user's arbitration and not a reading of any program.
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

} //namespace

class WagoDaliAddressRangeTest: public CalaosTest::CoreFixture
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

    //4.0 is announced, so the read carries the group flag in third position
    //(T3.156). Every expected frame below spells that flag out rather than
    //stopping at the address, so a frame truncated after the address reddens.
    void settleVersion(const std::string &host) const
    {
        Calaos::WagoMap::Instance(host, 502).udpRequest_cb(true, "WAGO_GET_VERSION 4.0 750-849");
    }

    Calaos::IOBase *makeDali(const std::string &host, const std::string &id,
                             const std::string &address)
    {
        Params p = {{ "type", "WODali" },
                    { "id", id },
                    { "name", "dali or dmx fixture" },
                    { "host", host },
                    { "line", "1" },
                    { "address", address },
                    { "group", "0" },
                    { "fade_time", "1" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        return createIO(p);
    }

    //The single frame a freshly built WODali has queued for the wire.
    std::string readFrameFor(const std::string &suffix, const std::string &id,
                             const std::string &address)
    {
        const std::string host = hostFor(suffix);
        EXPECT_NE(nullptr, makeDali(host, id, address));
        settleVersion(host);

        const std::vector<std::string> queued = framesOn(host);
        EXPECT_EQ(1u, queued.size())
            << "address " << address << " did not queue exactly one read";
        return queued.empty() ? std::string() : queued[0];
    }
};

/*----------------------------------------------------------------------------
 * ⭐ AN ADDRESS PAST THE DOCUMENTED CEILING IS STILL LOADED AND STILL SENT.
 *
 * This is what says that narrowing the documented ceiling from 612 to 355
 * cannot invalidate an installation that already declares 400: the ceiling is
 * read by the installer and by the generated documentation, never by the
 * loader. The control at 355 is what keeps "it loaded" from being a statement
 * about a factory that accepts everything.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliAddressRangeTest, AnAddressPastTheDocumentedCeilingStillLoads)
{
    //355: the last address the documentation now offers.
    EXPECT_EQ("WAGO_DALI_GET 1 355 0", readFrameFor("31", "dmx_at_ceiling", "355"));

    //400 and 612: outside it. 612 is what the documentation used to offer, so
    //an io.xml written against the old text carries exactly this.
    EXPECT_EQ("WAGO_DALI_GET 1 400 0", readFrameFor("32", "dmx_past_ceiling", "400"))
        << "an address outside the documented range no longer reaches the "
           "wire: an installation that already declares it has stopped "
           "working, and that is a decision nobody took";
    EXPECT_EQ("WAGO_DALI_GET 1 612 0", readFrameFor("33", "dmx_old_ceiling", "612"));
}

/*----------------------------------------------------------------------------
 * ⭐ AND THE ADDRESS IS COPIED, NEVER CONVERTED.
 *
 * If calaos_base derived the DMX channel itself, the base being exclusive
 * would move every channel by one and the documentation would be the least of
 * it. The pair below is what shows it does not: 105 and 5 both travel as
 * themselves, so no subtraction of a base happened to either.
 *--------------------------------------------------------------------------*/

TEST_F(WagoDaliAddressRangeTest, TheAddressTravelsVerbatimAndIsNeverTurnedIntoAChannel)
{
    EXPECT_EQ("WAGO_DALI_GET 1 105 0", readFrameFor("34", "dmx_channel_five", "105"))
        << "the address was converted into a DMX channel before being sent: "
           "the arithmetic belongs to the PLC and to nothing here";

    //The other side of the pair: a DALI short address travels as itself too,
    //so the line above is not satisfied by a frame that echoes any number.
    EXPECT_EQ("WAGO_DALI_GET 1 5 0", readFrameFor("35", "dali_short_five", "5"));
}
