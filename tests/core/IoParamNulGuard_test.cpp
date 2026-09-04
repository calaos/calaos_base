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
 * TWO ORACLES
 * ---------------------------------------------------------------------------
 *   G_  THE PARAMETER WRITE PATH of an IO: the API answer, the bytes on disk,
 *       and the model that comes back from a reload. No case here asserts a
 *       key order, an escaping or a depth.
 *
 *   M_  THE BYTE THAT IS NOT GUARDED, measured on the same path so that the
 *       reach of the guard is a fact and not a claim. One case, and it pins a
 *       defect that stays.
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

TEST_F(IoParamNulGuardTest, G_TheApiSaysTheParamWasRefused)
{
    /* ⭐ THE REFUSAL IS VISIBLE, and it has a message of its own. It used to
     * answer success, which is the half of the defect a client could have
     * noticed. "wrong io/param" is not reused: that one means the IO or the
     * parameter was not found, and a client that retried on it would be
     * chasing the wrong cause.
     */
    loadGuardHouse();

    const std::string body = setParamBody("\"name\\u0000squat\"", "\"squatted\"");

    EXPECT_TRUE(contains(body, "\"error\":\"param refused\"")) << body;
    EXPECT_FALSE(contains(body, "\"success\"")) << body;
}

TEST_F(IoParamNulGuardTest, G_AParamNameCarryingAZeroByteNoLongerRenamesTheIo)
{
    /* ⭐ THE REFERENCE CASE. pugi::xml_node::attribute(name.c_str()) stops at
     * the zero byte, so the parameter "name\0squat" used to be written as the
     * attribute "name" - the IO display name - and the IO came back renamed.
     * The rename was invisible until the configuration was read back, which is
     * why this asserts the RELOADED model and not the answer.
     */
    loadGuardHouse();

    ASSERT_TRUE(guardIo() != nullptr);
    ASSERT_EQ(std::string(T366_IO_NAME), guardIo()->get_param("name"));

    setParamBody("\"name\\u0000squat\"", "\"squatted\"");

    //Refused at the model boundary: the parameter is not even in memory.
    EXPECT_EQ(std::string(T366_IO_NAME), guardIo()->get_param("name"));
    EXPECT_FALSE(guardIo()->param_exists(NUL_IN_NAME))
            << "the parameter was stored and only the write refused it";

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr) << "the IO did not survive the reload";
    EXPECT_EQ(std::string(T366_IO_NAME), back->get_param("name"))
            << "the IO came back renamed";
}

TEST_F(IoParamNulGuardTest, G_TheNeighbourAttributeIsIntactOnDisk)
{
    /* The same case read on the file rather than on the model, attribute by
     * attribute: the squatting value used to REPLACE the neighbour instead of
     * landing beside it, so the count of `name="` is what tells "intact" from
     * "there are two of them now".
     */
    loadGuardHouse();

    setParamBody("\"name\\u0000squat\"", "\"squatted\"");
    saveConfig();

    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, std::string("name=\"") + T366_IO_NAME + "\""))
            << "the real name did not survive: " << xml;
    EXPECT_FALSE(contains(xml, "squatted"))
            << "the squatting value reached the configuration: " << xml;
    EXPECT_EQ(1u, occurrences(ioElement(xml), " name=\""))
            << "the IO carries a second name attribute: " << xml;
    EXPECT_EQ(std::string::npos, xml.find('\0'))
            << "a raw zero byte was written into the configuration file";
}

TEST_F(IoParamNulGuardTest, G_AParamValueCarryingAZeroByteNeverReachesTheFile)
{
    /* The value half. The parameter name is sound here, so nothing is
     * squatted: what used to be lost was everything after the zero byte, and
     * only at the next read. THE THREE ANSWERS TOLD APART: the parameter comes
     * back whole (impossible, XML has no zero byte), it comes back cut at
     * "head" (the defect), or it was never written (the guard).
     */
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"t366_nul\"", "\"head\\u0000tail\""),
                         "\"error\":\"param refused\""));

    ASSERT_TRUE(guardIo() != nullptr);
    EXPECT_FALSE(guardIo()->param_exists("t366_nul"));

    saveConfig();
    EXPECT_FALSE(contains(ioXmlOnDisk(), "t366_nul"));

    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_FALSE(back->param_exists("t366_nul"))
            << "a truncated value came back from the configuration: "
            << back->get_param("t366_nul");
}

TEST_F(IoParamNulGuardTest, G_TheWebsocketTransportRefusesTheSameZeroByte)
{
    //The WS half of the same door, kept apart because the two transports reach
    //set_param() through two different functions and a guard that missed one
    //of them would leave the other case green.
    loadGuardHouse();

    WsTestSession ws;
    const std::string answer = wsSetParam(ws, "\"name\\u0000wssquat\"",
                                          "\"wssquatted\"");
    EXPECT_TRUE(contains(answer, "\"error\":\"param refused\"")) << answer;

    /* ONE message and not two: set_param() raises an EventIOChanged when it
     * writes, and this session is subscribed to it. A refusal that still
     * announced the change would be caught right here.
     */
    pumpEventLoop();
    EXPECT_EQ(1u, ws.count()) << "the refusal still announced a change";

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(std::string(T366_IO_NAME), back->get_param("name"))
            << "the WS transport renamed the IO";
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

/*******************************************************************************
 * M_ - WHY THE GUARD IS ON THE ZERO BYTE AND ON NOTHING ELSE.
 *
 * The obvious worry is that the zero byte is one of a family, and that guarding
 * it alone leaves the other C0 controls to do the same damage. MEASURED: they
 * do not. pugixml writes them as a character reference, so nothing is cut and
 * no neighbour is touched - the zero byte is the one that breaks, because it
 * is the one that terminates the C string the writer resolves the name with.
 *
 * ⚠️ What stays wrong for the others is a smaller and different thing: XML 1.0
 * has no way to spell a C0 control, so `&#01;` is a reference no conforming
 * XML tool has to accept. pugixml reads back what it wrote, which is why this
 * case round trips; a third party editor opening io.xml may not. Widening the
 * guard to them is a SEPARATE decision - it would refuse bytes the API accepts
 * today - so it is measured here and written down, not fixed from this ticket.
 ******************************************************************************/

TEST_F(IoParamNulGuardTest, M_AnotherC0ControlByteIsEscapedAndSurvivesTheRoundTrip)
{
    loadGuardHouse();

    //0x01: not the zero byte, so the guard does not see it.
    EXPECT_TRUE(contains(setParamBody("\"t366_ctrl\"", "\"head\\u0001tail\""),
                         "\"success\":\"true\""))
            << "the guard widened to the other control bytes";

    saveConfig();
    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, "t366_ctrl=\"head&#01;tail\""))
            << "the control byte is not written as a character reference: " << xml;
    EXPECT_FALSE(contains(xml, std::string("head\x01tail")))
            << "the control byte was written raw: " << xml;

    //Nothing is cut and no neighbour is touched, which is the difference with
    //the zero byte and the reason this ticket stops there.
    EXPECT_TRUE(contains(xml, std::string("name=\"") + T366_IO_NAME + "\"")) << xml;

    reloadFromDisk();
    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(std::string("head\x01tail"), back->get_param("t366_ctrl"))
            << "the value did not survive the round trip";
}
