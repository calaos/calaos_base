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

//Output conditions on string IOs: the ones with a comparable state work, the
//ones without never match and say so once at load.

#include "CalaosCoreFixture.h"
#include "AudioPlayer.h"
#include "ConditionOutput.h"
#include "IPCam.h"
#include "OutputLightDimmer.h"
#include "OutputShutter.h"
#include "OutputString.h"
#include "RemoteUI.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

class TestOutputString: public OutputString
{
public:
    TestOutputString(Params &p):
        OutputString(p)
    {
    }

protected:
    void set_value_real(string) override {}
};

class TestShutter: public OutputShutter
{
public:
    TestShutter(Params &p):
        OutputShutter(p)
    {
    }

protected:
    void setOutputUp(bool) override {}
    void setOutputDown(bool) override {}
};

class TestDimmer: public OutputLightDimmer
{
public:
    TestDimmer(Params &p):
        OutputLightDimmer(p)
    {
    }

protected:
    bool set_value_real(int) override { return true; }
    bool set_dim_up_real(int) override { return true; }
};

} //namespace

class ConditionOutputStateTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
    }

    template <class T>
    T *add(const std::string &id)
    {
        Params p = { { "id", id }, { "name", id }, { "type", "Test" }, { "enabled", "true" }, { "visible", "true" }, { "time", "60" } };
        T *io = new T(p);
        firstRoom()->AddIO(io);
        ListeRoom::Instance().addIOHash(io);
        return io;
    }

    //Loads <calaos:output> exactly as a rule file does and returns the log.
    std::string load(ConditionOutput &cond, const std::string &id, const std::string &oper,
                     const std::string &val)
    {
        const std::string xml = "<calaos:condition type=\"output\" trigger=\"true\">"
                                "<calaos:output id=\"" +
                                id + "\" oper=\"" + oper +
                                "\" val=\"" + val + "\"/></calaos:condition>";
        pugi::xml_document doc;
        EXPECT_TRUE(doc.load_string(xml.c_str()));
        testing::internal::CaptureStdout();
        EXPECT_TRUE(cond.LoadFromXml(doc.first_child()));
        return testing::internal::GetCapturedStdout();
    }

    static bool warned(const std::string &log)
    {
        return log.find("this condition will never be true") != std::string::npos;
    }
};

TEST_F(ConditionOutputStateTest, OutputStringReadsBackWhatWasWritten)
{
    auto *o = add<TestOutputString>("str_out");
    EXPECT_EQ(o->get_value_string(), "");
    ASSERT_TRUE(o->set_value(std::string("hello")));
    EXPECT_EQ(o->get_value_string(), "hello");

    ConditionOutput eq;
    EXPECT_FALSE(warned(load(eq, "str_out", "==", "hello")));
    EXPECT_TRUE(eq.Evaluate());
    ConditionOutput ne;
    load(ne, "str_out", "!=", "hello");
    EXPECT_FALSE(ne.Evaluate());
}

TEST_F(ConditionOutputStateTest, StringIosWithoutAStateNeverMatchAndWarnAtLoad)
{
    add<IPCam>("camera");
    add<RemoteUI>("screen");

    for (const char *id: { "camera", "screen" })
    {
        for (const char *oper: { "==", "!=" })
        {
            ConditionOutput cond;
            const std::string log = load(cond, id, oper, "onplay");
            EXPECT_TRUE(warned(log)) << id << " " << oper << "\n"
                                     << log;
            EXPECT_FALSE(cond.Evaluate()) << id << " " << oper;
        }
    }
}

TEST_F(ConditionOutputStateTest, ChangedKeywordNeedsNoStateAndNeverWarns)
{
    add<AudioPlayer>("player");
    add<TestOutputString>("str_out");

    for (const char *id: { "player", "str_out" })
    {
        for (const char *oper: { "==", "!=", "SUP" })
        {
            ConditionOutput cond;
            const std::string log = load(cond, id, oper, "changed");
            EXPECT_FALSE(warned(log)) << id << " " << oper << "\n"
                                      << log;
            EXPECT_TRUE(cond.Evaluate()) << id << " " << oper;
        }
    }
}

TEST_F(ConditionOutputStateTest, ShutterAndDimmerKeepTheirOwnEqualityWithoutWarning)
{
    auto *shutter = add<TestShutter>("shutter");
    auto *dimmer = add<TestDimmer>("dimmer");

    for (IOBase *io: { static_cast<IOBase *>(shutter), static_cast<IOBase *>(dimmer) })
    {
        const std::string id = io->get_param("id");
        for (const char *val: { "stop", "true", "false" })
        {
            ConditionOutput eq;
            EXPECT_FALSE(warned(load(eq, id, "==", val))) << id << " " << val;
            EXPECT_EQ(eq.Evaluate(), io->check_condition_value(val, true)) << id << " " << val;

            ConditionOutput ne;
            EXPECT_FALSE(warned(load(ne, id, "!=", val))) << id << " " << val;
            EXPECT_EQ(ne.Evaluate(), io->check_condition_value(val, false)) << id << " " << val;
        }
    }
}
