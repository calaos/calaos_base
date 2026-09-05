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
 * WHAT AN UNWRITABLE BYTE DOES TO THE CONFIGURATION FILE, AND WHO IS TOLD.
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
 *   C_  THE EXACT SET OF REFUSED BYTES, asked of the API one code point at a
 *       time. XML 1.0 (Fifth Edition) §2.2 spells the grammar:
 *
 *         Char ::= #x9 | #xA | #xD | [#x20-#xD7FF] | [#xE000-#xFFFD]
 *                  | [#x10000-#x10FFFF]
 *
 *       so the refused set is the C0 controls MINUS tab, line feed and
 *       carriage return - #x0..#x8, #xB, #xC, #xE..#x1F - and nothing else.
 *       #x7F is a Char and stays accepted. "Nothing else" is the half a
 *       widened guard gets wrong, so the boundaries are driven, not argued.
 *
 *   L_  A CONFIGURATION THAT ALREADY CARRIES ONE. The guard is at the model
 *       boundary and NOT in the writer, and this is the whole difference: a
 *       house whose io.xml already spells &#01; must still load, and must
 *       still be saveable. A guard in the serializer would answer the API the
 *       same way and make that house impossible to re-record.
 *
 *   S_  THE SCENARIO NAME, which reaches io.xml WITHOUT set_param(): create
 *       hands a whole Params to ListeRoom::createIO(). No widening of the
 *       model guard can ever see it, so it has a guard of its own and this
 *       oracle is what says the two answer the same thing.
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
 * Ids are prefixed t366_ and t372_. Taken so far: e40_, e40b_ .. e40f_, e41b_, e41n_,
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

const char T372_ROOM_NAME[] = "T3.72 room";
const char T372_ROOM_TYPE[] = "cellar";
const char T372_IO_STRING[] = "t372_string";
const char T372_IO_NAME[] = "String value";

//The attribute a legacy io.xml already carries, and the value behind it. No
//probe of this file ever writes either: "it is still there" cannot be
//satisfied by something a case wrote a moment earlier.
const char T372_LEGACY_ATTR[] = "t372_legacy";
const std::string T372_LEGACY_VALUE =
        std::string("legacyhead") + '\x01' + "legacytail";
const char T372_LEGACY_ON_DISK[] = "t372_legacy=\"legacyhead&#01;legacytail\"";

//Assembled rather than spelled: a hex escape inside a literal swallows the
//following hex digits, and "the probe carries the byte I think it carries" is
//not something this file is willing to assume.
std::string withByte(const std::string &prefix, unsigned char c,
                     const std::string &suffix)
{
    return prefix + std::string(1, static_cast<char>(c)) + suffix;
}

//The \u00XX escape a JSON request spells the byte with.
std::string jsonEscapeOf(unsigned char c)
{
    static const char HEX[] = "0123456789abcdef";
    std::string e = "\\u00";
    e += HEX[(c >> 4) & 0xf];
    e += HEX[c & 0xf];
    return e;
}

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

    /* A house whose io.xml ALREADY spells a character reference for a C0
     * control, exactly as a Calaos that predates the guard would have written
     * it. It is loaded through the real Config path, so the byte arrives the
     * way an installed configuration delivers it and not the way a probe does.
     */
    void loadLegacyHouse()
    {
        loadConfig(ioXmlDocument(
                       roomXml(T372_ROOM_NAME, T372_ROOM_TYPE,
                               internalIoXml("InternalString", T372_IO_STRING,
                                             T372_IO_NAME,
                                             std::string(T372_LEGACY_ATTR) +
                                             "=\"legacyhead&#01;legacytail\""), 0)),
                   rulesXmlDocument(std::string()));
        pumpEventLoop();
    }

    static IOBase *legacyIo()
    {
        return ListeRoom::Instance().get_io(T372_IO_STRING);
    }

    //set_param over HTTP with a value carrying one arbitrary byte, spelled as
    //the JSON escape a client would send.
    static std::string setParamByte(const std::string &param, unsigned char c)
    {
        return setParamBody("\"" + param + "\"",
                            "\"head" + jsonEscapeOf(c) + "tail\"");
    }

    /* ⚠️ THE ANSWER IS NOT THE LAST MESSAGE, and reading it as such is how
     * this case first lied: a successful create raises EventScenarioAdded,
     * this session is subscribed to it, and pumping the loop pushes the event
     * BEHIND the reply. Both documents carry an "id", so an oracle written on
     * lastMessage() reads green off the event and never sees the answer.
     */
    static std::string replyTo(WsTestSession &ws, const std::string &msgId)
    {
        const std::string tag = "\"msg_id\":\"" + msgId + "\"";
        std::string reply;
        size_t n = 0;
        for (const std::string &m: ws.messages())
        {
            if (m.find(tag) == std::string::npos) continue;
            reply = m;
            n++;
        }
        EXPECT_EQ(1u, n) << "the request was not answered exactly once";
        return reply;
    }

    static std::string wsAutoscenario(WsTestSession &ws, const std::string &type,
                                      const Json &members)
    {
        Json data = members;
        data["type"] = type;
        ws.clear();
        ws.send(Json{{ "msg", "autoscenario" }, { "msg_id", "t372" },
                     { "data", data }});
        pumpEventLoop();
        return replyTo(ws, "t372");
    }

    static std::string idOf(const std::string &reply)
    {
        const Json envelope = Json::parse(reply, nullptr, false);
        if (!envelope.is_object()) return std::string();
        const Json data = envelope.value("data", Json::object());
        if (!data.is_object()) return std::string();
        return data.value("id", std::string());
    }

    static Json scenarioPayload(const std::string &name)
    {
        return Json{{ "name", name },
                    { "room_name", T366_ROOM_NAME },
                    { "room_type", T366_ROOM_TYPE },
                    { "steps", Json::array() }};
    }

    //How many IOs the house holds a scenario for, asked of the file rather
    //than of a guessed id: createIO() allocates the id itself.
    static size_t scenariosOnDisk(const std::string &xml)
    {
        return occurrences(xml, "type=\"scenario\"");
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
 * C_ - THE EXACT SET OF BYTES THE WRITE PATH REFUSES.
 *
 * The zero byte was guarded first because it is the one that CUTS: it ends the
 * C string the writer resolves an attribute name with. The others do not cut -
 * pugixml spells them as a character reference and reads its own back - and
 * that is precisely the trouble, because XML 1.0 has no way to spell a C0
 * control. `&#01;` is a reference to a character the grammar forbids, so the
 * file round trips through pugixml and through nothing else: an io.xml its
 * owner cannot open with his own tools.
 *
 * The refusal is therefore widened from "the byte that cuts" to "the bytes the
 * grammar has no spelling for", and the boundary is asked of the API one code
 * point at a time. ⚠️ Widening is the easy half; NOT widening too far is the
 * half a guard written from memory gets wrong - #x9, #xA and #xD are Char, and
 * so is #x7F.
 ******************************************************************************/

TEST_F(IoParamNulGuardTest, C_AControlByteInAParamValueNeverReachesTheFile)
{
    /* ⭐ THE REFERENCE CASE OF THE WIDENING, and the one that used to be
     * green the other way round: before, U+0001 was written as `&#01;` and
     * came back whole from the disk. Asserted on the reloaded model, not on
     * the answer, for the same reason as the zero byte: a guard that refused
     * the client and kept writing would pass a return code test.
     */
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"t372_ctrl\"", "\"head\\u0001tail\""),
                         "\"error\":\"param refused\""));

    ASSERT_TRUE(guardIo() != nullptr);
    EXPECT_FALSE(guardIo()->param_exists("t372_ctrl"))
            << "the parameter was stored and only the write refused it";

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    EXPECT_FALSE(contains(xml, "t372_ctrl")) << xml;
    EXPECT_FALSE(contains(xml, "&#01;"))
            << "a character reference XML 1.0 forbids reached the file: " << xml;

    reloadFromDisk();
    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_FALSE(back->param_exists("t372_ctrl"))
            << "the control byte came back from the configuration: "
            << back->get_param("t372_ctrl");
}

TEST_F(IoParamNulGuardTest, C_AControlByteInAParamNameIsRefusedToo)
{
    //The name half. It does not cut here - only the zero byte does - so the
    //damage without the guard is not a squatted neighbour but an attribute the
    //grammar cannot spell, sitting next to the good ones.
    loadGuardHouse();

    EXPECT_TRUE(contains(setParamBody("\"t372\\u0001name\"", "\"plain\""),
                         "\"error\":\"param refused\""));

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    EXPECT_FALSE(contains(xml, "plain\"")) << xml;
    EXPECT_EQ(1u, occurrences(ioElement(xml), " name=\""))
            << "the IO carries a second name attribute: " << xml;
}

TEST_F(IoParamNulGuardTest, C_TheBoundariesOfTheRefusedSetAreExactlyTheCharProduction)
{
    /* ⭐⭐ THE CASE THAT SAYS "NOTHING MORE". Each code point is driven through
     * the real request, and the two answers are told apart by the ERROR STRING
     * and not by its absence: "param refused" is the guard, "success" is the
     * write. A guard that refused everything below #x20 would be caught on
     * #x9/#xA/#xD; one that stopped at the zero byte would be caught on #x1.
     */
    struct Boundary { unsigned char byte; bool refused; const char *why; };
    static const Boundary BOUNDARIES[] = {
        { 0x00, true,  "NUL, the byte that cuts" },
        { 0x01, true,  "SOH, not in the Char production" },
        { 0x08, true,  "the last one below tab" },
        { 0x09, false, "tab IS in the Char production" },
        { 0x0a, false, "line feed IS in the Char production" },
        { 0x0b, true,  "vertical tab sits between two allowed ones" },
        { 0x0c, true,  "form feed, likewise" },
        { 0x0d, false, "carriage return IS in the Char production" },
        { 0x0e, true,  "the first one above carriage return" },
        { 0x1f, true,  "the last C0 control" },
        { 0x20, false, "space opens [#x20-#xD7FF]" },
        { 0x7f, false, "DEL is a Char in XML 1.0, only XML 1.1 restricts it" },
    };

    for (const Boundary &b: BOUNDARIES)
    {
        //A fresh house per code point: the answers are told apart by the byte
        //and not by what an earlier iteration left in the model.
        clearCoreState();
        loadGuardHouse();

        const std::string param = "t372_b" + std::to_string(static_cast<int>(b.byte));
        const std::string body = setParamByte(param, b.byte);

        if (b.refused)
        {
            EXPECT_TRUE(contains(body, "\"error\":\"param refused\""))
                    << "byte " << static_cast<int>(b.byte) << " (" << b.why
                    << ") was accepted: " << body;
            EXPECT_FALSE(guardIo()->param_exists(param))
                    << "byte " << static_cast<int>(b.byte) << " (" << b.why
                    << ") was stored anyway";
        }
        else
        {
            EXPECT_TRUE(contains(body, "\"success\":\"true\""))
                    << "byte " << static_cast<int>(b.byte) << " (" << b.why
                    << ") was refused: " << body;
            EXPECT_EQ(withByte("head", b.byte, "tail"),
                      guardIo()->get_param(param))
                    << "byte " << static_cast<int>(b.byte) << " (" << b.why
                    << ") did not reach the model whole";
        }
    }
}

TEST_F(IoParamNulGuardTest, C_TheThreeControlsXmlAllowsMakeTheWholeRoundTrip)
{
    /* The three exceptions read on the FILE and after a reload, which the
     * boundary case above does not do: accepting them at the model boundary
     * would be worth nothing if the writer lost them on the way out.
     */
    loadGuardHouse();

    const std::string value = std::string("a") + '\t' + "b" + '\n' + "c" + '\r' + "d";

    EXPECT_TRUE(contains(setParamBody("\"t372_ws\"", "\"a\\tb\\nc\\rd\""),
                         "\"success\":\"true\""));

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_EQ(value, back->get_param("t372_ws"))
            << "the three controls XML 1.0 allows did not survive the file";
}

TEST_F(IoParamNulGuardTest, C_TheWebsocketTransportRefusesTheSameControlByte)
{
    //The WS half of the widened door. The two transports reach set_param()
    //through two different functions, and the message COUNT is what catches a
    //refusal that would still announce the change it did not make.
    loadGuardHouse();

    WsTestSession ws;
    const std::string answer = wsSetParam(ws, "\"t372_wsctrl\"",
                                          "\"head\\u001ftail\"");
    EXPECT_TRUE(contains(answer, "\"error\":\"param refused\"")) << answer;

    pumpEventLoop();
    EXPECT_EQ(1u, ws.count()) << "the refusal still announced a change";

    saveConfig();
    reloadFromDisk();

    IOBase *back = guardIo();
    ASSERT_TRUE(back != nullptr);
    EXPECT_FALSE(back->param_exists("t372_wsctrl"));
}

/*******************************************************************************
 * L_ - THE HOUSE THAT ALREADY CARRIES ONE.
 *
 * ⛔ THE REAL RISK OF THIS TICKET, AND THE ONLY THING THAT DECIDES WHERE THE
 * GUARD GOES. Refusing at the write path is a compatibility break by design;
 * refusing in the serializer would be a different and much worse one, because
 * an installed io.xml that already spells &#01; would stop being recordable -
 * the next save of the whole configuration, for any unrelated reason, would
 * drop the attribute or fail.
 *
 * The guard sits at the model boundary (set_param, and the scenario name), so
 * IOFactory::readParams() and IOBase::SaveToXml() never meet it: an old house
 * loads, keeps its byte, and re-records it unchanged. What is refused is a NEW
 * value arriving through the API, and nothing else.
 ******************************************************************************/

TEST_F(IoParamNulGuardTest, L_AConfigurationAlreadyCarryingAControlByteStillLoads)
{
    loadLegacyHouse();

    IOBase *io = legacyIo();
    ASSERT_TRUE(io != nullptr) << "the IO did not survive the load";
    EXPECT_EQ(T372_LEGACY_VALUE, io->get_param(T372_LEGACY_ATTR))
            << "the guard refused a byte that came from the FILE";
}

TEST_F(IoParamNulGuardTest, L_AConfigurationAlreadyCarryingAControlByteStillSaves)
{
    /* ⭐ The whole point. A write the guard ACCEPTS is done first, so the save
     * that follows is the one an ordinary edit triggers - not a save invented
     * for this case - and the legacy attribute has to come through it intact.
     */
    loadLegacyHouse();

    HttpTestRequest req;
    LoginThrottle::clear();
    req.send(std::string("{\"cn_user\":\"") + apiUser() + "\",\"cn_pass\":\"" +
             apiPassword() + "\",\"action\":\"set_param\",\"id\":\"" +
             T372_IO_STRING + "\",\"param\":\"t372_edit\",\"value\":\"edited\"}");
    ASSERT_EQ(1u, req.count());
    EXPECT_TRUE(contains(req.body(), "\"success\":\"true\"")) << req.body();

    saveConfig();
    const std::string xml = ioXmlOnDisk();

    EXPECT_TRUE(contains(xml, T372_LEGACY_ON_DISK))
            << "the legacy attribute did not come back out of the writer: " << xml;
    EXPECT_TRUE(contains(xml, "t372_edit=\"edited\"")) << xml;

    reloadFromDisk();
    IOBase *back = legacyIo();
    ASSERT_TRUE(back != nullptr) << "the IO did not survive the round trip";
    EXPECT_EQ(T372_LEGACY_VALUE, back->get_param(T372_LEGACY_ATTR))
            << "an existing configuration stopped being recordable";
}

/*******************************************************************************
 * S_ - THE SCENARIO NAME, THE SECOND STOREY OF THE SAME DECISION.
 *
 * ⭐ WHY THE MODEL GUARD CANNOT ABSORB THIS ONE. buildAutoscenarioCreate()
 * fills a Params and hands it whole to ListeRoom::createIO(), which passes it
 * to the IO constructor: set_param() is never called, so no widening of it can
 * ever see the name. buildAutoscenarioModify() does call set_param("name"), so
 * it has been refusing since the zero byte guard - and the API answered two
 * different things to the same name depending on which verb carried it.
 *
 * ⛔ WHAT MUST NOT MOVE: the ACTIONS half of a scenario reaches io.xml through
 * AutoScenarioDef::saveToParams() at save time, not through either guard, and
 * an earlier ticket worked to let the byte cross the whole API there. The
 * witness at the end of this section is what says the boundary held.
 ******************************************************************************/

TEST_F(IoParamNulGuardTest, S_CreateRefusesAZeroByteInTheNameLikeModifyDoes)
{
    /* ⭐ THE CASE OF THE SECOND TICKET. `create` used to WRITE a truncated
     * name where `modify` refused it: same byte, same field, two answers.
     */
    loadGuardHouse();

    WsTestSession ws;
    const std::string answer =
            wsAutoscenario(ws, "create",
                           scenarioPayload(std::string("nul\0squat", 9)));

    EXPECT_TRUE(contains(answer, "\"error\":\"invalid payload: name refused\""))
            << answer;

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    EXPECT_EQ(0u, scenariosOnDisk(xml))
            << "a scenario was created despite the refusal: " << xml;
    EXPECT_FALSE(contains(xml, "name=\"nul\""))
            << "the truncated name reached the configuration: " << xml;
}

TEST_F(IoParamNulGuardTest, S_CreateRefusesAControlByteInTheNameToo)
{
    //The widening of the first ticket applied to the second storey: one guard
    //decides the set of bytes, two places consult it.
    loadGuardHouse();

    WsTestSession ws;
    const std::string answer =
            wsAutoscenario(ws, "create",
                           scenarioPayload(withByte("head", 0x01, "tail")));

    EXPECT_TRUE(contains(answer, "\"error\":\"invalid payload: name refused\""))
            << answer;

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    EXPECT_EQ(0u, scenariosOnDisk(xml)) << xml;
    EXPECT_FALSE(contains(xml, "&#01;")) << xml;
}

TEST_F(IoParamNulGuardTest, S_ModifyAnswersTheSameThingAsCreate)
{
    /* The comparison the second ticket exists for: the SAME name, refused by
     * the two verbs with the SAME document. A scenario is created with a sound
     * name first, so the modify path is reached for real.
     */
    loadGuardHouse();

    WsTestSession ws;
    const std::string created =
            wsAutoscenario(ws, "create", scenarioPayload("t372 sound"));
    ASSERT_TRUE(contains(created, "\"id\"")) << created;

    const std::string id = idOf(created);
    ASSERT_FALSE(id.empty()) << created;

    Json members = scenarioPayload(withByte("head", 0x01, "tail"));
    members["id"] = id;
    const std::string answer = wsAutoscenario(ws, "modify", members);

    EXPECT_TRUE(contains(answer, "\"error\":\"invalid payload: name refused\""))
            << answer;

    saveConfig();
    const std::string xml = ioXmlOnDisk();
    EXPECT_TRUE(contains(xml, "name=\"t372 sound\""))
            << "the scenario lost its sound name: " << xml;
    EXPECT_FALSE(contains(xml, "&#01;")) << xml;
}

TEST_F(IoParamNulGuardTest, S_AnActionCarryingAControlByteStillCrossesTheApi)
{
    /* ⛔ THE WITNESS, AND IT IS AN INVARIANT ON BOTH TREES. The actions of a
     * step are packed into one parameter at SAVE time, by a path neither guard
     * is on, and an earlier ticket deliberately made that byte cross the whole
     * API. This case is what tells "the guard was widened" from "the guard was
     * raised": if it ever goes red, the guard has been put too high.
     */
    loadGuardHouse();

    WsTestSession ws;
    Json members = scenarioPayload("t372 action");
    members["steps"] = Json::array({
        Json{{ "pause", "1" },
             { "actions", Json::array({
                   Json{{ "io", T366_IO_STRING },
                        { "value", withByte("a", 0x01, "b") }}
               }) }}
    });

    const std::string answer = wsAutoscenario(ws, "create", members);
    EXPECT_TRUE(contains(answer, "\"id\""))
            << "the action byte was refused, the guard is too high: " << answer;

    saveConfig();
    EXPECT_TRUE(contains(ioXmlOnDisk(), "&#01;"))
            << "the action byte no longer reaches the file: " << ioXmlOnDisk();
}
