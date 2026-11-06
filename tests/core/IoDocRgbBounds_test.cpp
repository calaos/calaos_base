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

/*******************************************************************************
 * The ioDoc of the two RGB drivers, read back the way `--gendoc` reads it.
 *
 * WHY THESE BOUNDS MATTER AT ALL: nothing in calaos_server ever consults them.
 * IODoc::paramAddInt() stores min/max in a Params that only genDocJson() and
 * genDocMd() read; IOBase::set_param() never looks. They are the ONLY thing
 * standing between an installer and a value the hardware will drop, so a bound
 * that lies is not a documentation defect - it is the whole guard.
 *
 * Measured against libola 0.10 in the dev image: DmxBuffer::Blackout() sizes
 * the buffer to 512 slots and SetChannel() silently ignores any index at or
 * above it. A channel the doc allowed but the buffer refuses costs the user a
 * colour that never lights and not one line of log on the path.
 *
 * WHY EVERY ORACLE HERE IS WRITTEN IN RELATION AND NEVER AS A CONSTANT: the
 * defect being pinned is a bound COPIED FROM THE NEIGHBOURING LINE. A case
 * that spelled the number out would go green the day the number moves, which
 * is the one day it must not. The three channels are compared to each other
 * and to the single-channel driver that documents the same physical thing;
 * a channel bound is required to DIFFER from the universe bound above it,
 * which is what makes "copied from the line above" visible at all.
 *
 * WHY THE ORACLE IS TYPE-STRICT: Params is a map<string,string>, so min/max
 * leave genDocJson() as JSON STRINGS ("512", never 512). Comparing Json values
 * refuses an int that slipped in where a string belongs.
 *
 * The IOs are built through IOFactory the way IOFactory::genDocIO() builds
 * them, under a ScopedDocGen so the throwaways stay out of the live io_table.
 * Both constructors reach a controller that spawns an external helper; the
 * helper does not exist next to the test binary, so uv_spawn fails cleanly -
 * same arrangement as core/KnxIo_test, custom main() included.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <unistd.h>

#include <map>
#include <memory>
#include <string>

#include "CalaosCoreFixture.h"
#include "IOBase.h"
#include "IODoc.h"
#include "IOFactory.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

typedef std::map<std::string, Json> DocParams;

DocParams docParams(IOBase *io)
{
    DocParams out;

    Json doc = io->getDoc()->genDocJson();
    for (const Json &p: doc.value("parameters", Json::array()))
    {
        if (p.is_object() && p.contains("name") && p.at("name").is_string())
            out[p.at("name").get<std::string>()] = p;
    }

    return out;
}

//Reading through this rather than through operator[] so that a parameter that
//was renamed fails as a missing name instead of as an empty comparison that
//two absent values would satisfy.
Json field(const DocParams &params, const std::string &name, const char *key)
{
    auto it = params.find(name);
    if (it == params.end())
        return Json();
    return it->second.value(key, Json());
}

std::string description(const DocParams &params, const std::string &name)
{
    const Json d = field(params, name, "description");
    return d.is_string() ? d.get<std::string>() : std::string();
}

//The word that follows `marker`, or the empty string when the marker is absent
//- which the callers assert on, so a reworded description cannot pass by
//making the needle disappear.
std::string wordAfter(const std::string &text, const std::string &marker)
{
    const size_t at = text.find(marker);
    if (at == std::string::npos)
        return std::string();

    size_t i = at + marker.size();
    std::string word;
    while (i < text.size() && (isalnum((unsigned char)text[i]) || text[i] == '_'))
        word += text[i++];

    return word;
}

} //namespace

class IoDocRgbBoundsTest: public CoreFixture
{
protected:
    //Alive for the whole case: it must cover the destruction of the
    //throwaways as well as their construction.
    std::unique_ptr<IOBase::ScopedDocGen> docScope;

    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig();
        docScope.reset(new IOBase::ScopedDocGen());
    }

    void TearDown() override
    {
        docScope.reset();
        CoreFixture::TearDown();
    }

    //The registry key is the lower-cased type name (IOFactory::RegisterClass).
    std::unique_ptr<IOBase> docIo(const char *registryKey, const char *typeName)
    {
        Params p;
        p.Add("type", typeName);
        p.Add("id", "doc");
        return std::unique_ptr<IOBase>(IOFactory::Instance().CreateIO(registryKey, p));
    }
};

/* ---------------------------------------------------------------------------
 * OLA - the three DMX channels
 * ------------------------------------------------------------------------ */

TEST_F(IoDocRgbBoundsTest, TheThreeDmxChannelsShareOneUpperBound)
{
    std::unique_ptr<IOBase> io = docIo("olaoutputlightrgb", "OLAOutputLightRGB");
    ASSERT_NE(nullptr, io.get());
    const DocParams params = docParams(io.get());

    for (const char *name: {"channel_red", "channel_green", "channel_blue"})
    {
        ASSERT_EQ(1u, params.count(name)) << name << " is not documented at all";
        EXPECT_TRUE(field(params, name, "max").is_string())
            << name << " publishes its max as " << field(params, name, "max").dump()
            << "; every leaf of io_doc.json is a JSON string";
        EXPECT_TRUE(field(params, name, "min").is_string()) << name;
    }

    EXPECT_EQ(field(params, "channel_red", "max"), field(params, "channel_green", "max"))
        << "the red DMX channel is bounded differently from the green one";
    EXPECT_EQ(field(params, "channel_red", "max"), field(params, "channel_blue", "max"))
        << "the red DMX channel is bounded differently from the blue one";
    EXPECT_EQ(field(params, "channel_green", "max"), field(params, "channel_blue", "max"));

    EXPECT_EQ(field(params, "channel_red", "min"), field(params, "channel_green", "min"));
    EXPECT_EQ(field(params, "channel_red", "min"), field(params, "channel_blue", "min"));
}

//The three channels agreeing on 9999 would satisfy the case above. What
//separates a channel from a universe is that they are NOT the same number, and
//the copy that produced the defect is exactly a channel wearing the universe's.
TEST_F(IoDocRgbBoundsTest, ADmxChannelIsNotBoundedLikeAUniverse)
{
    std::unique_ptr<IOBase> io = docIo("olaoutputlightrgb", "OLAOutputLightRGB");
    ASSERT_NE(nullptr, io.get());
    const DocParams params = docParams(io.get());

    ASSERT_EQ(1u, params.count("universe"));
    ASSERT_TRUE(field(params, "universe", "max").is_string());

    for (const char *name: {"channel_red", "channel_green", "channel_blue"})
    {
        ASSERT_EQ(1u, params.count(name));
        EXPECT_NE(field(params, "universe", "max"), field(params, name, "max"))
            << name << " carries the upper bound of the universe row above it";
    }
}

//OLAOutputLightDimmer documents the same physical thing on one channel, and it
//is the place of the tree that has always had it right. Tying the RGB bounds
//to it keeps both drivers honest without either of them naming a number.
TEST_F(IoDocRgbBoundsTest, TheRgbChannelsAgreeWithTheSingleChannelDriver)
{
    std::unique_ptr<IOBase> rgb = docIo("olaoutputlightrgb", "OLAOutputLightRGB");
    std::unique_ptr<IOBase> dimmer = docIo("olaoutputlightdimmer", "OLAOutputLightDimmer");
    ASSERT_NE(nullptr, rgb.get());
    ASSERT_NE(nullptr, dimmer.get());

    const DocParams rgbParams = docParams(rgb.get());
    const DocParams dimParams = docParams(dimmer.get());

    ASSERT_EQ(1u, dimParams.count("channel"));
    ASSERT_TRUE(field(dimParams, "channel", "max").is_string());

    for (const char *name: {"channel_red", "channel_green", "channel_blue"})
    {
        ASSERT_EQ(1u, rgbParams.count(name));
        EXPECT_EQ(field(dimParams, "channel", "max"), field(rgbParams, name, "max"))
            << name << " and the dimmer's single channel bound the same DMX slot number";
        EXPECT_EQ(field(dimParams, "channel", "min"), field(rgbParams, name, "min")) << name;
    }

    EXPECT_EQ(field(dimParams, "universe", "max"), field(rgbParams, "universe", "max"));
}

//A bound moves with a name when two paramAddInt lines are exchanged whole.
//The colour each row talks about is what says the payload stayed home.
TEST_F(IoDocRgbBoundsTest, EachChannelDescriptionNamesItsOwnColour)
{
    std::unique_ptr<IOBase> io = docIo("olaoutputlightrgb", "OLAOutputLightRGB");
    ASSERT_NE(nullptr, io.get());
    const DocParams params = docParams(io.get());

    const std::map<std::string, std::string> colourOf = {
        { "channel_red", "red" },
        { "channel_green", "green" },
        { "channel_blue", "blue" },
    };

    for (const auto &[name, colour]: colourOf)
    {
        const std::string desc = description(params, name);
        ASSERT_FALSE(desc.empty()) << name << " has no description";

        EXPECT_NE(std::string::npos, desc.find(colour))
            << name << " never says \"" << colour << "\": " << desc;

        for (const auto &[otherName, otherColour]: colourOf)
        {
            if (otherColour == colour)
                continue;
            EXPECT_EQ(std::string::npos, desc.find(otherColour))
                << name << " talks about " << otherColour << ": " << desc;
        }
    }
}

/* ---------------------------------------------------------------------------
 * MQTT - the two colour-space axes
 * ------------------------------------------------------------------------ */

//path_x and path_y are one description written twice. Both halves that carry
//the axis - the example path and the sentence that explains it - are read
//positionally: the marker must be found (asserted) and the token that follows
//it must be the parameter's own axis.
TEST_F(IoDocRgbBoundsTest, EachColourSpaceAxisPathReadsItsOwnAxis)
{
    std::unique_ptr<IOBase> io = docIo("mqttoutputlightrgb", "MqttOutputLightRGB");
    ASSERT_NE(nullptr, io.get());
    const DocParams params = docParams(io.get());

    for (const char *axis: {"x", "y"})
    {
        const std::string name = std::string("path_") + axis;
        ASSERT_EQ(1u, params.count(name)) << name << " is not documented at all";

        const std::string desc = description(params, name);
        ASSERT_FALSE(desc.empty()) << name << " has no description";

        const std::string example = wordAfter(desc, "color/");
        ASSERT_FALSE(example.empty())
            << name << " no longer shows a color/<axis> example: " << desc;
        EXPECT_EQ(std::string(axis), example)
            << name << " shows the example of the other axis: " << desc;

        const std::string read = wordAfter(desc, "read the ");
        ASSERT_FALSE(read.empty())
            << name << " no longer says which value it reads: " << desc;
        EXPECT_EQ(std::string(axis), read)
            << name << " says it reads the " << read << " value: " << desc;
    }
}

//The pair must stay a pair: two axes collapsed onto one description would
//satisfy every positional check above on whichever axis survived.
TEST_F(IoDocRgbBoundsTest, TheTwoAxisPathsAreTwoDistinctParameters)
{
    std::unique_ptr<IOBase> io = docIo("mqttoutputlightrgb", "MqttOutputLightRGB");
    ASSERT_NE(nullptr, io.get());
    const DocParams params = docParams(io.get());

    ASSERT_EQ(1u, params.count("path_x"));
    ASSERT_EQ(1u, params.count("path_y"));
    ASSERT_EQ(1u, params.count("path_brightness"));

    EXPECT_NE(description(params, "path_x"), description(params, "path_y"));
    EXPECT_NE(description(params, "path_x"), description(params, "path_brightness"));
    EXPECT_NE(description(params, "path_y"), description(params, "path_brightness"));
}

//Own main instead of gtest_main: OLACtrl and MqttBrokersList park their
//controllers in function-local statics whose ExternProcServer destructor
//kills pid 0 after a failed spawn, i.e. the whole process group - the automake
//harness included. Same reason as core/KnxIo_test.
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    fflush(nullptr);
    _exit(ret);
}
