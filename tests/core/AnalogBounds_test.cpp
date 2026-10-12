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

/*
 * `min` / `max` on the OutputAnalog family and on InternalInt.
 *
 * The bound applies to the value as written, BEFORE coeff_a/coeff_b/calc_expr:
 * it bounds a setpoint, not what the hardware receives. `step` is only an
 * increment, there is no grid to round to, so inc/dec compute value +/- step
 * and the result goes through the same clamp: no input can leave [min, max].
 */

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CalaosCoreFixture.h"
#include "CalaosConfig.h"
#include "IODoc.h"
#include "OutputAnalog.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

//Records what reaches the device so the clamp is observed where it matters.
class RecordingAnalog: public OutputAnalog
{
public:
    RecordingAnalog(Params &p): OutputAnalog(p) {}

    std::vector<double> sent;

    void set_value_real(double val) override { sent.push_back(val); }
    bool SaveToXml(pugi::xml_node) override { return true; }
};

std::unique_ptr<RecordingAnalog> makeAnalog(Params extra = Params())
{
    Params p = extra;
    p.Add("type", "RecordingAnalog");
    p.Add("id", "rec");
    p.Add("name", "rec");
    p.Add("enabled", "true");
    return std::unique_ptr<RecordingAnalog>(new RecordingAnalog(p));
}

std::set<std::string> docNames(IOBase *io)
{
    std::set<std::string> names;
    for (const Json &p: io->getDoc()->genDocJson().value("parameters", Json::array()))
        if (p.is_object() && p.contains("name") && p.at("name").is_string())
            names.insert(p.at("name").get<std::string>());
    return names;
}

} //namespace

class AnalogBoundsTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    IOBase *makeInt(const std::string &attrs)
    {
        loadConfig(ioXmlDocument(roomXml(ROOM_NAME, ROOM_TYPE,
                                         internalIoXml("InternalInt", "bounded", "Bounded", attrs))),
                   rulesXmlDocument(""));
        return io("bounded");
    }
};

TEST_F(AnalogBoundsTest, AbsentBoundsLeaveEveryValueUntouched)
{
    auto a = makeAnalog();
    for (double v: {-1e9, -3.5, 0.0, 42.0, 1e9})
    {
        a->set_value(v);
        EXPECT_EQ(v, a->sent.back());
        EXPECT_EQ(v, a->get_value_double());
    }
}

TEST_F(AnalogBoundsTest, MinAndMaxClampAWrittenValue)
{
    auto a = makeAnalog({{"min", "5"}, {"max", "30"}});

    a->set_value(-100.0);
    EXPECT_EQ(5.0, a->sent.back());
    EXPECT_EQ(5.0, a->get_value_double());
    EXPECT_EQ(5.0, a->get_command_double());

    a->set_value(100.0);
    EXPECT_EQ(30.0, a->sent.back());

    a->set_value(21.5);
    EXPECT_EQ(21.5, a->sent.back());
}

TEST_F(AnalogBoundsTest, TheBoundsThemselvesAreAccepted)
{
    auto a = makeAnalog({{"min", "5"}, {"max", "30"}});
    a->set_value(5.0);
    EXPECT_EQ(5.0, a->sent.back());
    a->set_value(30.0);
    EXPECT_EQ(30.0, a->sent.back());
}

TEST_F(AnalogBoundsTest, OneSidedBoundsLeaveTheOtherSideOpen)
{
    auto lo = makeAnalog({{"min", "0"}});
    lo->set_value(-4.0);
    EXPECT_EQ(0.0, lo->sent.back());
    lo->set_value(1e6);
    EXPECT_EQ(1e6, lo->sent.back());

    auto hi = makeAnalog({{"max", "10"}});
    hi->set_value(11.0);
    EXPECT_EQ(10.0, hi->sent.back());
    hi->set_value(-1e6);
    EXPECT_EQ(-1e6, hi->sent.back());
}

TEST_F(AnalogBoundsTest, StringWritesAreClampedToo)
{
    auto a = makeAnalog({{"min", "5"}, {"max", "30"}});
    EXPECT_TRUE(a->set_value(std::string("99")));
    EXPECT_EQ(30.0, a->sent.back());
}

TEST_F(AnalogBoundsTest, MinGreaterThanMaxIgnoresBothBounds)
{
    auto a = makeAnalog({{"min", "30"}, {"max", "5"}});
    a->set_value(-100.0);
    EXPECT_EQ(-100.0, a->sent.back());
    a->set_value(100.0);
    EXPECT_EQ(100.0, a->sent.back());
}

TEST_F(AnalogBoundsTest, ANonNumericBoundIsIgnoredAndTheOtherOneKept)
{
    auto a = makeAnalog({{"min", "abc"}, {"max", "10"}});
    a->set_value(-50.0);
    EXPECT_EQ(-50.0, a->sent.back());
    a->set_value(50.0);
    EXPECT_EQ(10.0, a->sent.back());

    auto b = makeAnalog({{"min", ""}, {"max", ""}});
    b->set_value(77.0);
    EXPECT_EQ(77.0, b->sent.back());
}

TEST_F(AnalogBoundsTest, BoundsApplyBeforeTheConversion)
{
    auto a = makeAnalog({{"max", "10"}, {"coeff_a", "2"}, {"coeff_b", "1"}});
    a->set_value(100.0);
    EXPECT_EQ(10.0, a->get_command_double());
    EXPECT_EQ(21.0, a->sent.back());
}

//Round-then-clamp does not exist here (step is an increment, not a grid);
//the value computed from step goes through the clamp, so it cannot escape.
TEST_F(AnalogBoundsTest, IncAndDecNeverLeaveTheBounds)
{
    auto a = makeAnalog({{"min", "0"}, {"max", "1"}, {"step", "0.4"}});

    a->set_value(0.8);
    a->set_value(std::string("inc"));
    EXPECT_EQ(1.0, a->get_value_double());
    a->set_value(std::string("inc"));
    EXPECT_EQ(1.0, a->get_value_double());

    a->set_value(0.3);
    a->set_value(std::string("dec"));
    EXPECT_EQ(0.0, a->get_value_double());
    a->set_value(std::string("dec 50"));
    EXPECT_EQ(0.0, a->get_value_double());
    a->set_value(std::string("inc 50"));
    EXPECT_EQ(1.0, a->get_value_double());

    for (double v: a->sent)
    {
        EXPECT_GE(v, 0.0);
        EXPECT_LE(v, 1.0);
    }
}

TEST_F(AnalogBoundsTest, WagoOutputAnalogIsBoundedThroughTheSameBase)
{
    IOBase *w = createIO({{"type", "WagoOutputAnalog"}, {"id", "wago"}, {"name", "wago"},
                          {"host", "192.0.2.51"}, {"var", "8"}, {"enabled", "true"},
                          {"visible", "true"}, {"min", "0"}, {"max", "100"}});
    ASSERT_NE(nullptr, w);

    w->set_value(250.0);
    EXPECT_EQ(100.0, w->get_value_double());
    w->set_value(-3.0);
    EXPECT_EQ(0.0, w->get_value_double());
    w->set_value(42.0);
    EXPECT_EQ(42.0, w->get_value_double());
}

TEST_F(AnalogBoundsTest, InternalIntWithoutBoundsIsUnchanged)
{
    IOBase *i = makeInt("");
    ASSERT_NE(nullptr, i);
    i->set_value(-1e6);
    EXPECT_EQ(-1e6, i->get_value_double());
    i->set_value(1e6);
    EXPECT_EQ(1e6, i->get_value_double());
}

TEST_F(AnalogBoundsTest, InternalIntIsClampedOnWriteAndOnIncDec)
{
    IOBase *i = makeInt("min=\"5\" max=\"30\" step=\"10\"");
    ASSERT_NE(nullptr, i);

    i->set_value(1000.0);
    EXPECT_EQ(30.0, i->get_value_double());
    i->set_value(-1000.0);
    EXPECT_EQ(5.0, i->get_value_double());

    i->set_value(25.0);
    i->set_value(std::string("inc"));
    EXPECT_EQ(30.0, i->get_value_double());
    i->set_value(std::string("dec 100"));
    EXPECT_EQ(5.0, i->get_value_double());
    i->set_value(std::string("12"));
    EXPECT_EQ(12.0, i->get_value_double());
}

TEST_F(AnalogBoundsTest, InternalIntWithMinGreaterThanMaxIsUnbounded)
{
    IOBase *i = makeInt("min=\"30\" max=\"5\"");
    ASSERT_NE(nullptr, i);
    i->set_value(1000.0);
    EXPECT_EQ(1000.0, i->get_value_double());
    i->set_value(-1000.0);
    EXPECT_EQ(-1000.0, i->get_value_double());
}

TEST_F(AnalogBoundsTest, InternalIntPersistsTheClampedValue)
{
    IOBase *i = makeInt("save=\"true\" min=\"5\" max=\"30\"");
    ASSERT_NE(nullptr, i);

    i->set_value(1000.0);

    std::string cached;
    ASSERT_TRUE(Config::Instance().ReadValueIO("bounded", cached));
    EXPECT_EQ("30", cached);
    EXPECT_EQ("30", i->get_param("value"));

    saveConfig();
    EXPECT_NE(std::string::npos, ioXmlOnDisk().find("value=\"30\""));
    reloadFromDisk();

    ASSERT_NE(nullptr, io("bounded"));
    EXPECT_EQ(30.0, io("bounded")->get_value_double());
}

//A value cached before the bounds existed, or edited by hand, must not come
//back out of bounds at boot.
TEST_F(AnalogBoundsTest, InternalIntRestoredFromCacheIsClamped)
{
    Config::Instance().SaveValueIO("restored", "500", false);
    Params p = {{"type", "InternalInt"}, {"id", "restored"}, {"name", "r"}, {"save", "true"},
                {"min", "5"}, {"max", "30"}, {"enabled", "true"}, {"visible", "true"}};
    IOBase *r = createIO(p);
    ASSERT_NE(nullptr, r);
    EXPECT_EQ(30.0, r->get_value_double());

    Config::Instance().SaveValueIO("restored2", "-9", false);
    Params p2 = {{"type", "InternalInt"}, {"id", "restored2"}, {"name", "r"}, {"save", "true"},
                 {"min", "5"}, {"max", "30"}, {"enabled", "true"}, {"visible", "true"}};
    IOBase *r2 = createIO(p2);
    ASSERT_NE(nullptr, r2);
    EXPECT_EQ(5.0, r2->get_value_double());
}

TEST_F(AnalogBoundsTest, TheBoundsAreDocumentedOnEveryTouchedType)
{
    IOBase *i = makeInt("");
    ASSERT_NE(nullptr, i);
    auto names = docNames(i);
    EXPECT_EQ(1u, names.count("min"));
    EXPECT_EQ(1u, names.count("max"));

    auto a = makeAnalog();
    names = docNames(a.get());
    EXPECT_EQ(1u, names.count("min"));
    EXPECT_EQ(1u, names.count("max"));

    //Bool and String internals are not bounded
    loadConfig();
    names = docNames(io(ID_BOOL_IN));
    EXPECT_EQ(0u, names.count("min"));
}
