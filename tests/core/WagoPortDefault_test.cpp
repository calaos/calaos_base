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
/*******************************************************************************
 * T3.25 (review reserve 1) - THE ORACLE THE WAGO `port` FAMILY DID NOT HAVE.
 *
 * The review measured that `WIAnalog`, `WITemp`, `WOAnalog`, `WODali`,
 * `WODaliRVB` and `WODigital` appear in NO file of tests/ at all. The nine
 * `Utils::from_string_or_keep(get_param("port"), port)` lines those six files
 * carry were therefore correct and pinned BY NOTHING: putting `from_string()`
 * back turns a present-but-blank `port` into Modbus port 0 and no test in the
 * tree says a word.
 *
 * ---------------------------------------------------------------------------
 * THE SEAM, and why it is the real one and not a proxy
 * ---------------------------------------------------------------------------
 * Every one of those nine lines exists for exactly one purpose: to choose the
 * port handed to `WagoMap::Instance(host, port)` on the very next statement.
 * `WagoMap` keys its singletons on the (host, port) PAIR and exposes the whole
 * table through the public `WagoMap::get_maps()`. So the port an IO actually
 * decided to talk to is directly observable, with no accessor added to
 * production and no mock in the way: after building the IO, look at which
 * WagoMap exists for its host.
 *
 *   `_or_keep`      blank port -> `port` stays 502 -> map (host, 502)
 *   `from_string`   blank port -> `port` becomes 0 -> map (host,   0)
 *
 * Each case uses its OWN host, so the maps of one case can never be mistaken
 * for another's, and `portsForHost()` returns the complete set - an assertion
 * on the set, never on a cardinal.
 *
 * ---------------------------------------------------------------------------
 * WHAT BUILDING A WagoMap DOES IN A TEST PROCESS, said plainly
 * ---------------------------------------------------------------------------
 * `WagoMap`'s constructor binds a UDP socket on `host` and spawns
 * `<bindir>/calaos_wago`. Both fail here, on purpose:
 *   - the hosts below are all in 192.0.2.0/24 (TEST-NET-1, RFC 5737), which is
 *     a syntactically valid IPv4 address that is never local: `bind()` fails
 *     with EADDRNOTAVAIL, uvw emits an ErrorEvent, and `udpProcessError()`
 *     only arms a CloseEvent handler. Using a TEST-NET address rather than
 *     127.0.0.1 also means this binary can never collide with a parallel
 *     `make check -j` peer over the fixed WAGO_LISTEN_PORT;
 *   - `calaos_wago` is not installed in the build tree, so the spawn fails and
 *     the respawn backoff arms a Timer.
 * CoreFixture never runs the libuv loop (see CalaosCoreFixture.h), so every one
 * of those handlers is created and never fired. The maps are deliberately NOT
 * destroyed: `stopAllWagoMaps()` would delete an ExternProcServer that pending
 * `Timer::singleShot` lambdas still capture, and process-lifetime singletons
 * are what production has anyway.
 ******************************************************************************/

#include "CalaosCoreFixture.h"

#include "IOFactory.h"
#include "WagoMap.h"
#include "WIAnalog.h"

#include <algorithm>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* The documented Modbus default, repeated here on purpose: the test must not
 * read it from the same constant the production code uses, or a change to that
 * constant would silently move the oracle with it. */
const int MODBUS_DEFAULT_PORT = 502;

/* Every port a WagoMap has been created for, for that host, in creation order.
 * The SET is what the assertions compare - never a cardinal. */
std::vector<int> portsForHost(const std::string &host)
{
    std::vector<int> ports;
    for (WagoMap *m: WagoMap::get_maps())
    {
        if (m->get_host() == host)
            ports.push_back(m->get_port());
    }
    return ports;
}

std::string portsToString(const std::vector<int> &ports)
{
    std::string s = "{";
    for (size_t i = 0; i < ports.size(); i++)
    {
        if (i) s += ", ";
        s += Utils::to_string(ports[i]);
    }
    return s + "}";
}

::testing::AssertionResult onlyTalksTo(const std::string &host, int expected)
{
    const std::vector<int> ports = portsForHost(host);

    if (ports.size() == 1 && ports[0] == expected)
        return ::testing::AssertionSuccess();

    return ::testing::AssertionFailure()
            << "host " << host << " ended up with WagoMap ports "
            << portsToString(ports) << ", expected exactly {" << expected << "}";
}

/* WIAnalog::readValue() re-reads "port" on every poll (WIAnalog.cpp:113), and
 * it is protected. The probe exposes it and changes NOTHING else: the object
 * under test is the production WIAnalog, built by the production constructor.
 */
class T325WIAnalogProbe: public WIAnalog
{
public:
    T325WIAnalogProbe(Params &p): WIAnalog(p) {}
    void probeReadValue() { readValue(); }
};

void registerT325WagoProbe()
{
    static bool done = false;
    if (done) return;
    done = true;

    IOFactory::Instance().RegisterClass(
        "T325WIAnalogProbe", [](Params &p) -> IOBase * { return new T325WIAnalogProbe(p); });
}

}

class WagoPortDefaultTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        registerT325WagoProbe();
        CoreFixture::SetUp();
        loadConfig();
    }

    /* One host per case, so the WagoMap table stays unambiguous across the
     * whole binary. TEST-NET-1, see the file header. */
    std::string hostFor(const std::string &suffix) const
    {
        return "192.0.2." + suffix;
    }

    IOBase *makeWagoIo(const std::string &type, const std::string &host,
                       bool withPortParam, const std::string &port)
    {
        Params p = {{ "type", type },
                    { "id", "t325_" + type + "_" + host },
                    { "name", type + " under test" },
                    { "host", host },
                    { "var", "12" },
                    { "enabled", "true" },
                    { "visible", "true" }};
        if (withPortParam)
            p.Add("port", port);
        return createIO(p);
    }
};

/*******************************************************************************
 * THE SIX CONSTRUCTORS. One case per family, all six with a `port` parameter
 * that is PRESENT and BLANK - the exact shape that gets past the
 * `get_params().Exists("port")` guard and that a plain from_string() turns
 * into port 0.
 ******************************************************************************/

TEST_F(WagoPortDefaultTest, AnAnalogInputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("11");
    ASSERT_NE(nullptr, makeWagoIo("WIAnalog", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WIAnalog.cpp:52 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, ATempInputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("12");
    ASSERT_NE(nullptr, makeWagoIo("WITemp", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WITemp.cpp:52 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, AnAnalogOutputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("13");
    ASSERT_NE(nullptr, makeWagoIo("WOAnalog", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WOAnalog.cpp:50 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, ADigitalOutputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("14");
    ASSERT_NE(nullptr, makeWagoIo("WODigital", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WODigital.cpp:54 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, ADaliOutputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("15");
    ASSERT_NE(nullptr, makeWagoIo("WODali", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WODali.cpp:53 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, ADaliRvbOutputWithABlankPortKeepsTheModbusDefault)
{
    const std::string host = hostFor("16");
    ASSERT_NE(nullptr, makeWagoIo("WODaliRVB", host, true, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WODaliRVB.cpp:66 no longer protects the Modbus default";
}

/*******************************************************************************
 * THE TWO CONTROLS. Without them "keep the default" could be satisfied by
 * never reading the parameter at all.
 ******************************************************************************/

TEST_F(WagoPortDefaultTest, AnAbsentPortParameterKeepsTheModbusDefaultToo)
{
    //GREEN BEFORE AND AFTER T3.25: the Exists() guard already covered this
    //shape. It is here so that a "simplification" dropping the guard has an
    //oracle as well.
    const std::string host = hostFor("17");
    ASSERT_NE(nullptr, makeWagoIo("WOAnalog", host, false, ""));
    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT));
}

TEST_F(WagoPortDefaultTest, AConfiguredPortIsStillTheOneUsed)
{
    //GREEN BEFORE AND AFTER: a readable port must still WIN over the default,
    //which is what tells `_or_keep` apart from "ignore the parameter".
    const std::string host = hostFor("18");
    ASSERT_NE(nullptr, makeWagoIo("WOAnalog", host, true, "1502"));
    EXPECT_TRUE(onlyTalksTo(host, 1502));
}

/*******************************************************************************
 * THE THREE RE-READS. WIAnalog::readValue(), WOAnalog::set_value_real() and
 * WODigital::set_value_real() parse "port" AGAIN on every poll/write, from
 * three more `_or_keep` lines. The parameter is blanked AFTER a well formed
 * load, so the constructor's line cannot be the one under test: the member
 * already holds 502 and only the re-read can move it.
 ******************************************************************************/

TEST_F(WagoPortDefaultTest, AnAnalogInputPollKeepsTheModbusDefaultWhenPortIsBlanked)
{
    const std::string host = hostFor("21");
    IOBase *base = makeWagoIo("T325WIAnalogProbe", host, true, "502");
    ASSERT_NE(nullptr, base);
    T325WIAnalogProbe *io = dynamic_cast<T325WIAnalogProbe *>(base);
    ASSERT_NE(nullptr, io);
    ASSERT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT));

    io->set_param("port", "");
    io->probeReadValue();

    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WIAnalog.cpp:113 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, AnAnalogOutputWriteKeepsTheModbusDefaultWhenPortIsBlanked)
{
    const std::string host = hostFor("22");
    IOBase *io = makeWagoIo("WOAnalog", host, true, "502");
    ASSERT_NE(nullptr, io);
    ASSERT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT));

    io->set_param("port", "");
    io->set_value(42.0);

    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WOAnalog.cpp:108 no longer protects the Modbus default";
}

TEST_F(WagoPortDefaultTest, ADigitalOutputWriteKeepsTheModbusDefaultWhenPortIsBlanked)
{
    const std::string host = hostFor("23");
    IOBase *io = makeWagoIo("WODigital", host, true, "502");
    ASSERT_NE(nullptr, io);
    ASSERT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT));

    io->set_param("port", "");
    io->set_value(true);

    EXPECT_TRUE(onlyTalksTo(host, MODBUS_DEFAULT_PORT))
            << "WODigital.cpp:129 no longer protects the Modbus default";
}
