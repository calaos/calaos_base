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
 * WHAT A ZERO BYTE DOES TO THE CONFIGURATION FILE, AND WHO IS TOLD ABOUT IT.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS NEXT TO core/JsonApiRequestGuards_test
 * ---------------------------------------------------------------------------
 * That file measured WHERE the escaped NUL lands once the request parser lets
 * it in: it asserted the bytes of io.xml right after a save, and it stopped
 * there because no guard was its business.
 *
 * This file asks the two questions a guard is judged on, and neither can be
 * answered by reading a return code:
 *
 *   - WHAT COMES BACK FROM DISK. Every case here reloads the configuration and
 *     asserts the reloaded model attribute by attribute. A guard that made
 *     set_param() answer false while the file kept being corrupted would pass
 *     any test written on the return value alone, and would fix nothing.
 *   - WHAT THE CLIENT IS TOLD. A refusal nobody can observe is one more
 *     silence, so the answer document is asserted on both transports.
 *
 * ---------------------------------------------------------------------------
 * ONE ORACLE
 * ---------------------------------------------------------------------------
 *   G_  THE PARAMETER WRITE PATH of an IO: the API answer, the bytes on disk,
 *       and the model that comes back from a reload. No case here asserts a
 *       key order, an escaping or a depth.
 *
 * ---------------------------------------------------------------------------
 * ANTI "FIXTURE PAUVRE"
 * ---------------------------------------------------------------------------
 *   - the reference case is the RENAME, and it is asserted on the reloaded IO
 *     rather than on the file: "name=" appears in io.xml for every IO of the
 *     house, so a substring search alone would prove nothing about THIS one;
 *   - the neighbour "name" carries a value ("String value") that no probe ever
 *     writes, so "the neighbour survived" cannot be satisfied by accident;
 *   - the probes are asymmetric ("head" before the zero byte, "tail" after,
 *     "name" before and "squat" after): a truncation, a drop and a replacement
 *     are three different answers;
 *   - a plain parameter with no zero byte is written by the SAME requests and
 *     asserted through the SAME reload, so "nothing is ever stored" cannot be
 *     read as "the zero byte was refused";
 *   - both transports are driven, because the WS handler and the HTTP handler
 *     reach set_param() through two different functions.
 *
 * This file adds NO GOLDEN. tests/core/golden must keep the tree hash it has.
 *
 * Ids are prefixed t366_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
 * e41o_, e41p_, e41q_, e41r_, e41s_, e46b_, t317a_ .. t317f_, t318_, t319_,
 * t358_.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"
#include "IOBase.h"

#include <string>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char T366_ROOM_NAME[] = "T3.66 room";
const char T366_ROOM_TYPE[] = "salon";
const char T366_IO_STRING[] = "t366_string";

//The display name the house gives the IO. No probe of this file ever writes
//it, so finding it after a refused write really means the write was refused.
const char T366_IO_NAME[] = "String value";

//Built with an explicit length: a plain literal would stop at the zero byte
//and the probe would BE the truncation it is meant to detect.
const std::string NUL_INSIDE = std::string("head\0tail", 9);
const std::string NUL_IN_NAME = std::string("name\0squat", 10);

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

//The <calaos:internal .../> element alone. The room element carries a name=
//attribute too, and counting attributes over the whole document would count
//that one and answer the wrong question.
std::string ioElement(const std::string &xml)
{
    const size_t at = xml.find("<calaos:internal ");
    if (at == std::string::npos) return std::string();
    return xml.substr(at, xml.find("/>", at) - at);
}

//How many times `needle` occurs. Used to prove that the squatting attribute
//did not simply land NEXT to the real one.
size_t occurrences(const std::string &haystack, const std::string &needle)
{
    size_t n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size()))
        n++;
    return n;
}

} //namespace

class IoParamNulGuardTest: public JsonApiCharacterizationTest
{
protected:
    /* One string IO in one room. Everything here is about a byte inside a
     * parameter, not about the shape of a house.
     */
    void loadGuardHouse()
    {
        loadConfig(ioXmlDocument(roomXml(T366_ROOM_NAME, T366_ROOM_TYPE,
                                         internalIoXml("InternalString",
                                                       T366_IO_STRING,
                                                       T366_IO_NAME), 0)),
                   rulesXmlDocument(std::string()));
        pumpEventLoop();
    }

    //A RAW HTTP body with the credentials spelled in by hand: every probe here
    //has to survive as TEXT, and authenticated(Json) would dump it.
    static std::string httpRawBody(const std::string &members)
    {
        return std::string("{\"cn_user\":\"") + apiUser() +
               "\",\"cn_pass\":\"" + apiPassword() + "\"" +
               (members.empty()? std::string(): "," + members) + "}";
    }

    /* ⚠️ A refused request registers a login FAILURE, and the next request
     * from the same address inside the backoff window answers 400 whatever it
     * says - so a case that sends a refusal then a probe would measure the
     * throttle. It has a suite of its own; it is not this file's oracle.
     */
    static std::string httpBodyFor(const std::string &rawBody)
    {
        LoginThrottle::clear();
        HttpTestRequest req;
        req.send(rawBody);
        EXPECT_EQ(1u, req.count());
        return req.body();
    }

    //set_param over HTTP, param name and value spelled as raw JSON text so a
    //probe can carry an escape.
    static std::string setParamBody(const std::string &rawParam,
                                    const std::string &rawValue)
    {
        return httpBodyFor(httpRawBody(std::string("\"action\":\"set_param\",\"id\":\"") +
                                       T366_IO_STRING + "\",\"param\":" + rawParam +
                                       ",\"value\":" + rawValue));
    }

    //The same over the websocket, which reaches set_param() through another
    //function entirely.
    static std::string wsSetParam(WsTestSession &ws, const std::string &rawParam,
                                  const std::string &rawValue)
    {
        ws.clear();
        ws.send(std::string("{\"msg\":\"set_param\",\"msg_id\":\"1\",\"data\":{\"id\":\"") +
                T366_IO_STRING + "\",\"param\":" + rawParam +
                ",\"value\":" + rawValue + "}}");
        return ws.lastMessage();
    }

    static IOBase *guardIo()
    {
        return ListeRoom::Instance().get_io(T366_IO_STRING);
    }
};

/*******************************************************************************
 * G_ - THE PARAMETER WRITE PATH.
 ******************************************************************************/

TEST_F(IoParamNulGuardTest, G_TheApiAnswersAParamNameCarryingAZeroByte)
{
    //What a client is told today. The write is destructive and the answer is
    //indistinguishable from the answer to a write that worked.
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"name\\u0000squat\"", "\"squatted\""),
                         "\"success\":\"true\""))
            << "the answer to a param name carrying a zero byte changed";
}

TEST_F(IoParamNulGuardTest, G_AParamNameCarryingAZeroByteRenamesTheIoAfterAReload)
{
    /* ⭐ THE REFERENCE CASE. pugi::xml_node::attribute(name.c_str()) stops at
     * the zero byte, so the parameter "name\0squat" is written as the
     * attribute "name" - the IO display name. The rename is invisible until
     * the configuration is read back, which is why the assertion is on the
     * RELOADED model and not on the answer.
     */
    loadGuardHouse();

    ASSERT_TRUE(guardIo() != nullptr);
    ASSERT_EQ(std::string(T366_IO_NAME), guardIo()->get_param("name"));

    setParamBody("\"name\\u0000squat\"", "\"squatted\"");

    //In memory the two parameters coexist: Params is a map and the keys differ.
    EXPECT_EQ(std::string(T366_IO_NAME), guardIo()->get_param("name"));
    EXPECT_EQ(std::string("squatted"), guardIo()->get_param(NUL_IN_NAME));

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr) << "the IO did not survive the reload";
    EXPECT_EQ(std::string("squatted"), back->get_param("name"))
            << "the IO kept the name it was given by the configuration";
}

TEST_F(IoParamNulGuardTest, G_TheNeighbourAttributeDoesNotSurviveTheWrite)
{
    /* The same defect read on the file rather than on the model, attribute by
     * attribute: the squatting value REPLACES the neighbour instead of landing
     * beside it, so the count of `name="` stays at one and its content changes.
     */
    loadGuardHouse();

    setParamBody("\"name\\u0000squat\"", "\"squatted\"");
    saveConfig();

    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, "name=\"squatted\""))
            << "the squatting attribute is not on disk: " << xml;
    EXPECT_FALSE(contains(xml, std::string("name=\"") + T366_IO_NAME + "\""))
            << "the real name survived, so this case measures nothing: " << xml;
    EXPECT_EQ(1u, occurrences(ioElement(xml), " name=\""))
            << "the two parameters landed as two attributes: " << xml;
    EXPECT_EQ(std::string::npos, xml.find('\0'))
            << "a raw zero byte was written into the configuration file";
}

TEST_F(IoParamNulGuardTest, G_AParamValueCarryingAZeroByteIsCutAtTheZeroByte)
{
    //The value half. The parameter name is sound here, so nothing is squatted:
    //what is lost is everything after the zero byte, and only at the next read.
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"t366_nul\"", "\"head\\u0000tail\""),
                         "\"success\":\"true\""));

    ASSERT_TRUE(guardIo() != nullptr);
    ASSERT_EQ(NUL_INSIDE, guardIo()->get_param("t366_nul"));

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_TRUE(back->param_exists("t366_nul"))
            << "the parameter did not reach the configuration at all";
    EXPECT_EQ(std::string("head"), back->get_param("t366_nul"))
            << "the value that came back is not the truncated one";
}

TEST_F(IoParamNulGuardTest, G_TheWebsocketTransportWritesTheSameZeroByte)
{
    //The WS half of the same door, kept apart because the two transports reach
    //set_param() through two different functions and a change that missed one
    //of them would leave the other case green.
    loadGuardHouse();

    WsTestSession ws;
    EXPECT_TRUE(contains(wsSetParam(ws, "\"name\\u0000wssquat\"", "\"wssquatted\""),
                         "\"success\":\"true\""))
            << "the WS answer to a param name carrying a zero byte changed";
    pumpEventLoop();

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(std::string("wssquatted"), back->get_param("name"))
            << "the WS transport did not rename the IO";
}

TEST_F(IoParamNulGuardTest, G_APlainParamMakesTheWholeRoundTrip)
{
    /* THE CONTROL, and it is an INVARIANT: it is green before any guard and it
     * must stay green after. Without it every case above could be read as "the
     * XML writer loses parameters", which is not what is being measured.
     */
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"t366_plain\"", "\"headtail\""),
                         "\"success\":\"true\""));

    saveConfig();
    EXPECT_TRUE(contains(ioXmlOnDisk(), "t366_plain=\"headtail\""));

    reloadFromDisk();
    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(std::string("headtail"), back->get_param("t366_plain"));
    EXPECT_EQ(std::string(T366_IO_NAME), back->get_param("name"))
            << "a sound write disturbed the neighbour";
}
