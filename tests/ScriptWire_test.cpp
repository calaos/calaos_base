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
 * E4.1j - CHARACTERIZATION of the DOWNSTREAM half of the Lua wire.
 *
 * Two programs, one protocol, both ends in this repository:
 *
 *   calaos_server  LuaScript/ScriptExec.cpp          emits "execute"/"event",
 *                                                    decodes what comes back
 *                                                    (STILL JANSSON - E4.1m)
 *   calaos_script  LuaScript/ScriptExtern_main.cpp   decodes "execute"/"event",
 *                                                    emits "finished"
 *                  LuaScript/ScriptBindings.cpp      emits {msg,data} on every
 *                                                    Lua binding that talks
 *                                                    back to the server
 *
 * This file covers the calaos_script half ONLY. ScriptExec.cpp is out of
 * perimeter on purpose (E4.1m): the wire carries JSON TEXT and both ends run a
 * real parser, so a nlohmann emitter and a jansson receiver understand each
 * other perfectly. The mixed state is supported; what would NOT be supported is
 * a change of STRUCTURE, and there is none here.
 *
 * THE SEAM is the free functions of the anonymous namespace below. In the
 * characterization commit they carry the jansson bodies of the two production
 * files VERBATIM; the migration commit rewires them onto the shipped header
 * LuaScript/ScriptWire.h, so that from then on a mutation of the SHIPPED
 * emitter or of the SHIPPED decoder turns this suite RED. A test that
 * re-implements the assembly instead of calling it only freezes what the TEST
 * does - measured on a sibling ticket of this series, where 34/34 stayed green
 * while the production dump() was put back naked. Every assertion that moves
 * when the seam is rewired is flagged in place with MOVED BY E4.1j.
 *
 * WHY NEITHER FILE CAN BE CALLED DIRECTLY: ScriptExtern_main.cpp defines
 * main() through EXTERN_PROC_CLIENT_MAIN, and ScriptBindings.cpp only reaches
 * the wire through a Lua_Calaos/LuaIOBase holding a live ExternProcClient,
 * i.e. a connected unix socket to calaos_server. Free functions can be called;
 * that is why the wire lives in a header - same shape as IO/Wago/WagoWire.h,
 * IO/Reolink/ReolinkWire.h and IO/Mqtt/MqttWire.h next door.
 *
 * ****************************************************************************
 * WHERE THE BYTES OF THIS WIRE COME FROM - AND HERE THE ANSWER IS NOT THE ONE
 * WAGO, OLA AND HUE GAVE. On those wires no uncontrolled byte could reach a
 * dump() and the two byte oracles were DEFENSIVE. On this wire they are
 * LOAD BEARING, and the injection channel is one line of user Lua:
 *
 *     calaos.sendPushNotif(string.char(0xFF))        -- "message"
 *     calaos.setIOValue("io_x", string.char(0xFF))   -- "value"
 *     calaos.setIOParam("io_x", "k", string.char(0xFF))
 *
 * A Lua string is a byte string; LuaJIT is Lua 5.1, so string.char() and the
 * "\255" decimal escape both exist. Those bytes are handed to lua_tostring()
 * and land STRAIGHT in a JSON string value - see ScriptBindings.cpp:411/416/
 * 456-487. The script is written by the user and arrives through rules.xml or
 * the JSON API.
 *
 * ⚠️ PRECISION, measured: the two spellings above work TODAY because the
 * script stays pure ASCII and the byte is born inside the Lua VM. A RAW
 * invalid byte written in the SCRIPT TEXT does NOT survive the trip today -
 * ScriptExec.cpp:154-157 sends the script through jansson_from_params(), whose
 * json_string() answers NULL on it and DROPS the whole pair, so calaos_script
 * never receives that script at all. That third channel OPENS with E4.1m,
 * which migrates ScriptExec.cpp. Do not cite it as reachable before then.
 *
 * So the KNX precedent applies here in full: raw bytes reaching a NAKED dump()
 * throw type_error.316, and there is no try/catch anywhere on this path -
 * messageReceived() is called from the ExternProc read callback of a process
 * whose only job is to run the script. A naked dump() here is std::terminate
 * on calaos_script, i.e. the script dies mid-run and the server sees the pipe
 * close. That is why error_handler_t::replace is not decoration.
 * ****************************************************************************
 *
 * WHAT IS ASSERTED ON BYTES, and why it has to be: the 145 goldens of the
 * series compare PARSED DOCUMENTS and are blind to escaping by construction.
 * Two sibling tickets MEASURED that flipping ensure_ascii true->false, or
 * swapping replace for ignore, left whole suites green. Three cases below
 * therefore look at the RAW BYTES, and they pin the three spellings SEPARATELY:
 * replace writes �, ignore makes the bytes DISAPPEAR, strict THROWS.
 *
 * A DELIBERATELY RICH FIXTURE. The recurring defect of the E4.0/E4.1 series is
 * the "poor fixture": a dataset too uniform for a swap of two interchangeable
 * fields to show. The context below carries TWO IOs whose every field differs
 * and which are not substrings of one another, and the env carries TWO keys
 * with two different values, so exchanging either pair is visible.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Utils.h"
#include "Params.h"

//E4.1j MIGRATION COMMIT: the seams below no longer carry a copy of the
//assembly, they call the SHIPPED header. From here on, a mutation of the
//production emitter or of the production decoder turns this suite RED.
#include "ScriptWire.h"

using std::string;
using std::vector;

namespace
{

/*---------------------------------------------------------------------------
 * Helpers
 *-------------------------------------------------------------------------*/

bool isPureAscii(const string &s)
{
    for (size_t i = 0;i < s.size();i++)
        if (static_cast<unsigned char>(s[i]) > 0x7f)
            return false;
    return true;
}

//Readable failure messages: a raw wire with a 0xff in it is unprintable.
string escaped(const string &s)
{
    string out;
    char buf[8];
    for (size_t i = 0;i < s.size();i++)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c >= 0x20 && c < 0x7f)
            out += static_cast<char>(c);
        else
        {
            snprintf(buf, sizeof(buf), "<%02x>", c);
            out += buf;
        }
    }
    return out;
}

/*---------------------------------------------------------------------------
 * THE SEAM - messages EMITTED by calaos_script.
 *
 * Bodies copied VERBATIM from ScriptBindings.cpp:405-436 / :455-495 and from
 * ScriptExtern_main.cpp:130-135. The migration commit replaces each body with
 * a one line call into LuaScript/ScriptWire.h.
 *-------------------------------------------------------------------------*/

//ScriptBindings.cpp, the envelope shared by BOTH sendJson() members before
//E4.1j (Lua_Calaos and LuaIOBase had the same body, duplicated). Both are now
//one call into the shipped header.

//ScriptBindings.cpp, sendPushNotif() - one argument form
string wirePushNotif(const string &message)
{
    return ScriptWire::buildPushNotifMessage(message);
}

//ScriptBindings.cpp, sendPushNotif() - two arguments form
string wirePushNotif(const string &message, const string &attachment)
{
    return ScriptWire::buildPushNotifMessage(message,
                                             ScriptWire::PushAttachment{attachment});
}

//ScriptBindings.cpp, the three LuaIOBase::set_value() overloads. They differ
//only in how they turn the Lua value into a string before this point, so one
//seam covers the three: bool -> "true"/"false", double ->
//Utils::to_string(double), string -> as is.
string wireSetState(const string &id, const string &value)
{
    return ScriptWire::buildSetStateMessage(ScriptWire::IoId{id}, value);
}

//ScriptBindings.cpp, LuaIOBase::set_param()
string wireSetParam(const string &id, const string &key, const string &value)
{
    return ScriptWire::buildSetParamMessage(ScriptWire::IoId{id},
                                            ScriptWire::ParamKey{key},
                                            ScriptWire::ParamValue{value});
}

//ScriptExtern_main.cpp - the ONLY message calaos_script sends on its own
//initiative. Note it is FLAT: no "data" envelope.
string wireFinished(bool ret)
{
    return ScriptWire::buildFinishedMessage(ret);
}

/*---------------------------------------------------------------------------
 * THE SEAM - messages DECODED by calaos_script.
 *
 * Bodies copied VERBATIM from ScriptExtern_main.cpp:43-60 (the parse guard),
 * :56-77 and :131 (the "execute" path) and :139-153 (the "event" path).
 *
 * The signatures are deliberately LIBRARY NEUTRAL - std::string in, Params /
 * vector<Params> / std::string out - so that not one assertion of this file
 * has to change shape when the bodies go from jansson to nlohmann.
 *-------------------------------------------------------------------------*/

//ScriptExtern_main.cpp: the parse guard. There was no JSON_DECODE_ANY, so a
//top level scalar did not even parse, and a top level array parsed but failed
//json_is_object(). Both are still refused.
bool wireAcceptMessage(const string &msg)
{
    Json jroot;
    return ScriptWire::parseMessage(msg, jroot);
}

//The DEFAULT CONTRACT, driven from the wire text: the default comes back when
//the key is ABSENT and when the value is NOT A STRING. It never throws.
//ScriptExtern_main.cpp depends on it three times, and ScriptExec.cpp:81 (out
//of perimeter) passes a NON EMPTY default.
string wireStringGet(const string &msg, const string &key, const string &def)
{
    Json jroot;
    if (!ScriptWire::parseMessage(msg, jroot))
        return def;
    return ScriptWire::stringGet(jroot, key, def);
}

//The FLATTENING CONTRACT, driven from the wire text.
void wireDecodeObject(const string &msg, Params &out)
{
    const Json jroot = Json::parse(msg, nullptr, false);
    if (jroot.is_discarded())
        return;
    ScriptWire::decodeObject(jroot, out);
}

//ScriptExtern_main.cpp - the whole "execute" decode.
bool wireDecodeExecute(const string &msg, string &script, vector<Params> &ios, Params &env)
{
    Json jroot;
    if (!ScriptWire::parseMessage(msg, jroot))
        return false;

    script = ScriptWire::stringGet(jroot, "script");

    const vector<Params> decoded = ScriptWire::decodeContext(jroot);
    ios.insert(ios.end(), decoded.begin(), decoded.end());

    ScriptWire::decodeEnv(jroot, env);

    return true;
}

/*
 * ScriptExtern_main.cpp - the whole "event" decode, INCLUDING the pitfall this
 * ticket exists to not fall into: the walk goes two levels down a document it
 * does not own, and the key may be absent. ScriptWire::decodeEvent() takes the
 * root by CONST reference so that the CREATING overload of operator[] cannot
 * be reached from it.
 *
 * root_after is the root serialized AFTER the decode ran: that is how the test
 * sees a document that grew a key, since the outputs stay empty either way.
 */
bool wireDecodeEvent(const string &msg, Params &ev, string &type_str, string *root_after = nullptr)
{
    Json jroot;
    if (!ScriptWire::parseMessage(msg, jroot))
        return false;

    ScriptWire::decodeEvent(jroot, ev, type_str);

    if (root_after)
        *root_after = ScriptWire::dumpJson(jroot);

    return true;
}

/*---------------------------------------------------------------------------
 * THE FIXTURES
 *
 * Rich on purpose. The two IOs of the context share NO value, and neither
 * value is a substring of the other, so exchanging them is visible in every
 * assertion. Same for the two env keys. The context also carries a JSON
 * boolean and a JSON number so that the flattening contract is exercised by
 * the real message and not only by a synthetic case.
 *-------------------------------------------------------------------------*/

const char FX_EXECUTE[] =
    "{\"msg\":\"execute\","
    "\"script\":\"calaos.sendPushNotif('bonjour')\\n\","
    "\"context\":["
      "{\"id\":\"io_kitchen_light\",\"name\":\"Lampe cuisine\",\"var_type\":\"bool\","
       "\"state\":true,\"gui_type\":\"light\"},"
      "{\"id\":\"io_garage_probe\",\"name\":\"Sonde garage\",\"var_type\":\"float\","
       "\"state\":18.5,\"gui_type\":\"analog_in\"}"
    "],"
    "\"env\":{\"caller\":\"rule_evening\",\"reason\":\"scheduled_wakeup\"}}";

const char FX_EVENT[] =
    "{\"msg\":\"event\",\"data\":{"
      "\"type_str\":\"io_changed\",\"type\":\"5\","
      "\"data\":{\"id\":\"io_garage_probe\",\"state\":\"19.5\",\"name\":\"Sonde garage\"}"
    "}}";

//The pitfall fixture: an "event" whose "data" key is ABSENT. Kept to a SINGLE
//key so that its serialization is identical under both libraries and the
//root_after assertion does not have to move at the migration.
const char FX_EVENT_NO_DATA[] = "{\"msg\":\"event\"}";

} //namespace

/*******************************************************************************
 * EMISSION - the exact bytes calaos_script puts on the wire
 ******************************************************************************/

/*
 * MOVED BY E4.1j - and this is the ONE structural byte change of this ticket.
 * jansson walked the top level object in INSERTION order ("msg" then "data"),
 * nlohmann::json sorts ("data" then "msg"). The "data" sub object does NOT
 * move: it is built from a Params, which is a std::map, so jansson_from_params
 * already walked it ALPHABETICALLY. Nothing on the other end can see it -
 * ScriptExec.cpp decodes with json_loads(), a real parser.
 */
TEST(ScriptWire, PushNotifWithAMessageOnly)
{
    const string wire = wirePushNotif("Le portail est ouvert");

    EXPECT_EQ("{\"data\":{\"message\":\"Le portail est ouvert\"},\"msg\":\"send_push_notif\"}",
              wire) << escaped(wire);
}

//MOVED BY E4.1j (top level order only). "attachment" before "message" inside
//"data" is NOT a change: Params is a std::map.
TEST(ScriptWire, PushNotifWithAnAttachment)
{
    const string wire = wirePushNotif("Le portail est ouvert", "http://cam/snap.jpg");

    EXPECT_EQ("{\"data\":"
              "{\"attachment\":\"http://cam/snap.jpg\",\"message\":\"Le portail est ouvert\"},"
              "\"msg\":\"send_push_notif\"}",
              wire) << escaped(wire);
}

//MOVED BY E4.1j (top level order only).
TEST(ScriptWire, SetStateCarriesTheIdAndTheValue)
{
    const string wire = wireSetState("io_kitchen_light", "true");

    EXPECT_EQ("{\"data\":{\"id\":\"io_kitchen_light\",\"value\":\"true\"},"
              "\"msg\":\"set_state\"}",
              wire) << escaped(wire);
}

/*
 * MOVED BY E4.1j (top level order only).
 *
 * set_value(double) goes through Utils::to_string(double), a BARE ostringstream:
 * six significant digits, scientific notation past that. 1234.56789 comes out
 * "1234.57". That is frozen ON PURPOSE - goldens elsewhere in the tree pin the
 * same truncation - and this ticket does not "fix" it.
 */
TEST(ScriptWire, SetStateOfADoubleGoesThroughUtilsToString)
{
    const string wire = wireSetState("io_garage_probe", Utils::to_string(1234.56789));

    EXPECT_EQ("{\"data\":{\"id\":\"io_garage_probe\",\"value\":\"1234.57\"},"
              "\"msg\":\"set_state\"}",
              wire) << escaped(wire);
}

//MOVED BY E4.1j (top level order only).
TEST(ScriptWire, SetParamCarriesIdParamAndValue)
{
    const string wire = wireSetParam("io_garage_probe", "log_history", "true");

    EXPECT_EQ("{\"data\":"
              "{\"id\":\"io_garage_probe\",\"param\":\"log_history\",\"value\":\"true\"},"
              "\"msg\":\"set_param\"}",
              wire) << escaped(wire);
}

/*
 * NOT moved by the migration, and that is the point: the "finished" message is
 * built from a Params and serialized straight, with no envelope. Params is a
 * std::map, so jansson already emitted "msg" then "return_val" ALPHABETICALLY,
 * which is exactly what nlohmann does. This string is byte identical before
 * and after E4.1j.
 */
TEST(ScriptWire, FinishedCarriesTheReturnValueAsAWord)
{
    EXPECT_EQ("{\"msg\":\"finished\",\"return_val\":\"true\"}", wireFinished(true));
    EXPECT_EQ("{\"msg\":\"finished\",\"return_val\":\"false\"}", wireFinished(false));
}

/*
 * TYPE STRICT. Every value of this wire is a JSON STRING - the booleans and
 * the numbers included. ScriptExec.cpp reads them back with
 * jansson_string_get() / jansson_decode_object(), and a value that became a
 * real JSON number or a real JSON boolean would read back as the DEFAULT, i.e.
 * as nothing at all. 3 is not "3".
 */
TEST(ScriptWire, EveryValueOfTheWireIsAJsonString)
{
    const Json setState = Json::parse(wireSetState("io_kitchen_light", "true"), nullptr, false);
    ASSERT_FALSE(setState.is_discarded());
    EXPECT_TRUE(setState.at("msg").is_string());
    EXPECT_TRUE(setState.at("data").at("id").is_string());
    EXPECT_TRUE(setState.at("data").at("value").is_string());
    EXPECT_FALSE(setState.at("data").at("value").is_boolean());

    const Json numeric = Json::parse(wireSetState("io_garage_probe", Utils::to_string(42)),
                                     nullptr, false);
    ASSERT_FALSE(numeric.is_discarded());
    EXPECT_TRUE(numeric.at("data").at("value").is_string());
    EXPECT_FALSE(numeric.at("data").at("value").is_number());
    EXPECT_EQ("42", numeric.at("data").at("value").get<string>());

    const Json fin = Json::parse(wireFinished(true), nullptr, false);
    ASSERT_FALSE(fin.is_discarded());
    EXPECT_TRUE(fin.at("return_val").is_string());
    EXPECT_FALSE(fin.at("return_val").is_boolean());
}

//The envelope has EXACTLY two keys, and "data" is an object. A third key, or a
//"data" flattened into the root, would be a change of structure - the one
//thing the mixed jansson/nlohmann state of this wire could not absorb.
TEST(ScriptWire, TheEnvelopeHasExactlyTwoKeysAndDataIsAnObject)
{
    const Json j = Json::parse(wireSetParam("io_x", "k", "v"), nullptr, false);
    ASSERT_FALSE(j.is_discarded());
    EXPECT_EQ(2u, j.size());
    EXPECT_TRUE(j.contains("msg"));
    EXPECT_TRUE(j.at("data").is_object());
    EXPECT_EQ(3u, j.at("data").size());
}

/*******************************************************************************
 * THE TWO BYTE ORACLES - LOAD BEARING ON THIS WIRE, NOT DEFENSIVE
 *
 * Read the header of this file: a Lua script written by the user can put ANY
 * byte into "message", "value" or the param value, with one call. These two
 * cases are the only thing in the tree that can see it.
 ******************************************************************************/

/*
 * RED IF ensure_ascii IS EVER DROPPED. Without it nlohmann writes the raw UTF-8
 * bytes and the wire stops being pure ASCII - which jansson's JSON_ENSURE_ASCII
 * never allowed.
 *
 * Reachable in production: calaos.sendPushNotif("Le portail est ouvert") with
 * any accent in it. Notification texts are French prose; this is the common
 * case, not the exotic one.
 */
TEST(ScriptWire, TheWireStaysPureAsciiWhenALuaScriptSendsAnAccent)
{
    const string wire = wirePushNotif("Caf\xc3\xa9 \xc3\x80lpha"); //U+00E9 then U+00C0

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);

    //MOVED BY E4.1j: jansson escaped with UPPERCASE hex, nlohmann with
    //LOWERCASE hex. That is the ONE byte difference of this migration on a
    //non-ASCII value, and no JSON parser can see it.
    EXPECT_NE(string::npos, wire.find("\\u00e9")) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\u00c0")) << escaped(wire);
    EXPECT_EQ(string::npos, wire.find("\\u00E9")) << escaped(wire);

    //and it round trips to the very same UTF-8 it came from
    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);
    EXPECT_EQ("Caf\xc3\xa9 \xc3\x80lpha", j.at("data").at("message").get<string>());
}

/*
 * RED IF error_handler_t::replace IS EVER DROPPED.
 *
 * Reachable in production with ONE line of user Lua:
 *     calaos.sendPushNotif(string.char(0xFF))
 * A naked dump() would throw type_error.316 out of the ExternProc read
 * callback, where nothing catches: std::terminate, calaos_script dies in the
 * middle of the user's script. E4.1e watched exactly that kill a KNX driver on
 * a dimmer at 78%.
 */
TEST(ScriptWire, InvalidUtf8FromALuaScriptDoesNotAbortTheEmission)
{
    string wire;
    ASSERT_NO_THROW(wire = wirePushNotif("push-\xff-tail"))
            << "the emitter threw on invalid UTF-8 coming from a Lua script";

    EXPECT_TRUE(isPureAscii(wire)) << escaped(wire);

    const Json j = Json::parse(wire, nullptr, false);
    ASSERT_FALSE(j.is_discarded()) << escaped(wire);

    /*
     * MOVED BY E4.1j. Under jansson, json_string() answered NULL on the bad
     * byte, json_object_set_new() answered -1, neither return code was tested
     * anywhere, and the WHOLE PAIR was dropped on the floor: the server got
     * send_push_notif with an EMPTY data object and pushed an empty
     * notification. Under nlohmann the key is there, with the bad byte turned
     * into U+FFFD.
     */
    ASSERT_TRUE(j.at("data").contains("message")) << escaped(wire);
    EXPECT_EQ(1u, j.at("data").size()) << escaped(wire);
    EXPECT_NE(string::npos, wire.find("\\ufffd")) << escaped(wire);
    EXPECT_EQ("push-\xef\xbf\xbd-tail", j.at("data").at("message").get<string>());

    //MOVED BY E4.1j - and this is what ties the SHIPPED emitter to the
    //"replace" column of the table in
    //Tripwire_TheThreeErrorHandlerSpellingsAreThreeDifferentBytestreams.
    //RED under ::ignore (the byte disappears, "push--tail"), RED under
    //::strict (the ASSERT_NO_THROW above), RED without ensure_ascii (the raw
    //U+FFFD bytes instead of the escape).
    Json probe;
    probe["message"] = "push-\xff-tail";
    EXPECT_EQ("{\"data\":" + probe.dump(-1, ' ', true, Json::error_handler_t::replace) +
              ",\"msg\":\"send_push_notif\"}", wire) << escaped(wire);
    EXPECT_NE("{\"data\":" + probe.dump(-1, ' ', true, Json::error_handler_t::ignore) +
              ",\"msg\":\"send_push_notif\"}", wire) << escaped(wire);
}

/*
 * TRIPWIRE - the three spellings of error_handler_t are THREE DIFFERENT
 * BYTESTREAMS, pinned SEPARATELY. This is what makes "RED if the handler
 * disappears" mean "RED for the RIGHT reason": a suite that only checked
 * "no throw" would stay green under ::ignore, which silently EATS the byte.
 *
 * This case drives nlohmann directly, so it holds before and after the
 * migration; the case above is what ties the SHIPPED emitter to the "replace"
 * column of this table.
 */
TEST(ScriptWire, Tripwire_TheThreeErrorHandlerSpellingsAreThreeDifferentBytestreams)
{
    Json j;
    j["message"] = "push-\xff-tail";

    // replace : one U+FFFD per rejected byte, escaped because ensure_ascii
    const string replaced = j.dump(-1, ' ', true, Json::error_handler_t::replace);
    EXPECT_NE(string::npos, replaced.find("\\ufffd")) << escaped(replaced);
    EXPECT_EQ("{\"message\":\"push-\\ufffd-tail\"}", replaced) << escaped(replaced);

    // ignore : the rejected byte simply DISAPPEARS. Note it is NOT the same
    // observable as jansson's drop - jansson dropped the whole PAIR, ignore
    // keeps the pair and shortens the value.
    const string ignored = j.dump(-1, ' ', true, Json::error_handler_t::ignore);
    EXPECT_EQ(string::npos, ignored.find("\\ufffd")) << escaped(ignored);
    EXPECT_EQ("{\"message\":\"push--tail\"}", ignored) << escaped(ignored);
    EXPECT_LT(ignored.size(), replaced.size());

    // strict (the default) : THROWS type_error.316
    EXPECT_THROW(j.dump(-1, ' ', true, Json::error_handler_t::strict), Json::type_error);
    EXPECT_THROW(j.dump(), Json::type_error);

    // and the three are pairwise different bytestreams
    EXPECT_NE(replaced, ignored);
}

/*******************************************************************************
 * DECODING - the "execute" message
 ******************************************************************************/

TEST(ScriptWire, ExecuteYieldsTheScriptTheContextAndTheEnv)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute(FX_EXECUTE, script, ios, env));

    EXPECT_EQ("calaos.sendPushNotif('bonjour')\n", script);
    EXPECT_EQ(2u, ios.size());
    EXPECT_EQ(2, env.size());
}

/*
 * COUNTER MUTATION BY EXCHANGE, target 1: swap the two IOs of the context in
 * the fixture and every assertion below fails. They share no value and neither
 * is a substring of the other.
 */
TEST(ScriptWire, TheContextKeepsItsTwoIosDistinctAndInOrder)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute(FX_EXECUTE, script, ios, env));
    ASSERT_EQ(2u, ios.size());

    EXPECT_EQ("io_kitchen_light", ios[0]["id"]);
    EXPECT_EQ("Lampe cuisine", ios[0]["name"]);
    EXPECT_EQ("bool", ios[0]["var_type"]);
    EXPECT_EQ("light", ios[0]["gui_type"]);
    EXPECT_EQ("true", ios[0]["state"]);       //JSON boolean -> the WORD "true"

    EXPECT_EQ("io_garage_probe", ios[1]["id"]);
    EXPECT_EQ("Sonde garage", ios[1]["name"]);
    EXPECT_EQ("float", ios[1]["var_type"]);
    EXPECT_EQ("analog_in", ios[1]["gui_type"]);
    EXPECT_EQ("18.5", ios[1]["state"]);       //JSON number -> Utils::to_string(double)

    //belt and braces against a fixture that would drift into uniformity
    EXPECT_NE(ios[0]["id"], ios[1]["id"]);
    EXPECT_NE(ios[0]["name"], ios[1]["name"]);
    EXPECT_NE(ios[0]["state"], ios[1]["state"]);
}

/*
 * COUNTER MUTATION BY EXCHANGE, target 2: swap the two values of the env and
 * both assertions fail. The two keys differ AND the two values differ.
 */
TEST(ScriptWire, TheEnvKeepsItsTwoKeysDistinct)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute(FX_EXECUTE, script, ios, env));

    EXPECT_EQ("rule_evening", env["caller"]);
    EXPECT_EQ("scheduled_wakeup", env["reason"]);
    EXPECT_NE(env["caller"], env["reason"]);
}

//"context" absent: json_array_size(NULL) is 0, the loop never runs.
TEST(ScriptWire, ExecuteWithoutAContextYieldsNoIo)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute("{\"msg\":\"execute\",\"script\":\"x=1\"}", script, ios, env));
    EXPECT_EQ("x=1", script);
    EXPECT_TRUE(ios.empty());
    EXPECT_EQ(0, env.size());
}

//"context" present but NOT an array: same, no iteration, no throw.
TEST(ScriptWire, ExecuteWithAContextThatIsNotAnArrayYieldsNoIo)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute("{\"msg\":\"execute\",\"context\":{\"id\":\"io_x\"}}",
                                  script, ios, env));
    EXPECT_TRUE(ios.empty());
}

//"env" present but NOT an object: no iteration, no throw, env stays empty.
TEST(ScriptWire, ExecuteWithAnEnvThatIsNotAnObjectLeavesTheEnvEmpty)
{
    string script;
    vector<Params> ios;
    Params env;

    ASSERT_TRUE(wireDecodeExecute("{\"msg\":\"execute\",\"env\":[1,2]}", script, ios, env));
    EXPECT_EQ(0, env.size());
}

/*******************************************************************************
 * DECODING - the "event" message, and the operator[] pitfall
 ******************************************************************************/

TEST(ScriptWire, EventYieldsItsTypeAndItsFlattenedData)
{
    Params ev;
    //Same sentinel as the case below, on the POPULATED path this time: a
    //decoder that only ADDS to ev would come back with four parameters here.
    ev.Add("__sentinel_key_that_is_never_an_event_parameter__", "__sentinel_value__");
    string type_str;

    ASSERT_TRUE(wireDecodeEvent(FX_EVENT, ev, type_str));

    EXPECT_EQ("io_changed", type_str);
    EXPECT_EQ(3, ev.size()) << "the sentinel survived: ev was not cleared";
    EXPECT_FALSE(ev.Exists("__sentinel_key_that_is_never_an_event_parameter__"));
    EXPECT_EQ("io_garage_probe", ev["id"]);
    EXPECT_EQ("19.5", ev["state"]);
    EXPECT_EQ("Sonde garage", ev["name"]);

    //"type_str" lives on the EVENT, "id" lives on its inner "data". Reading
    //type_str one level too deep would give "" here.
    EXPECT_NE(type_str, ev["id"]);
}

/*
 * ⭐ THE PITFALL OF THIS TICKET.
 *
 * ScriptExtern_main.cpp:148-153 calls json_object_get(jev, "data") on a jev
 * that CAN BE NULL, and jansson answers NULL without crashing. The nlohmann
 * translation must NOT be operator[]: on a non const Json, jroot["data"]
 * CREATES the key and turns the null into an object, so a document that had no
 * "data" grows one - silently, and invisibly to any assertion that only looks
 * at the outputs, because the outputs stay empty either way.
 *
 * root_after is that assertion: the root, serialized AFTER the decode ran. It
 * must still be the single key document it came in as.
 */
TEST(ScriptWire, EventWithoutDataYieldsNothingAndDoesNotGrowTheDocument)
{
    /*
     * TWO SENTINELS, one per output, and neither can ever become a legal value:
     * no event type is spelled like this, and no event parameter is keyed like
     * this. They are here because both outputs are OUT PARAMETERS the caller
     * owns: the decoder has to leave them EMPTY on a message that carries
     * nothing, not merely "not add to them". Reviewed defect: without the ev
     * sentinel, dropping ev.clear() from the production decoder went unseen.
     */
    Params ev;
    ev.Add("__sentinel_key_that_is_never_an_event_parameter__", "__sentinel_value__");
    string type_str = "__sentinel_that_can_never_be_a_type__";
    string root_after;

    ASSERT_NO_THROW({
        ASSERT_TRUE(wireDecodeEvent(FX_EVENT_NO_DATA, ev, type_str, &root_after));
    });

    EXPECT_EQ(0, ev.size())
            << "the sentinel survived: ev was left as it came in";
    EXPECT_FALSE(ev.Exists("__sentinel_key_that_is_never_an_event_parameter__"));
    EXPECT_EQ("", type_str)
            << "the sentinel survived: type_str was left as it came in";

    //THE DETECTOR of the operator[] pitfall: one key in, one key out.
    EXPECT_EQ("{\"msg\":\"event\"}", root_after) << escaped(root_after);
}

//"data" present but not an object: json_object_get answers NULL on it too.
TEST(ScriptWire, EventWithADataThatIsNotAnObjectYieldsNothing)
{
    Params ev;
    string type_str = "sentinel_that_can_never_be_a_type";

    ASSERT_TRUE(wireDecodeEvent("{\"msg\":\"event\",\"data\":\"not_an_object\"}", ev, type_str));
    EXPECT_EQ(0, ev.size());
    EXPECT_EQ("", type_str);
}

//The event is there but carries no inner "data": type_str still comes out.
TEST(ScriptWire, EventWithoutAnInnerDataStillYieldsItsType)
{
    Params ev;
    string type_str;

    ASSERT_TRUE(wireDecodeEvent("{\"msg\":\"event\",\"data\":{\"type_str\":\"io_added\"}}",
                                ev, type_str));
    EXPECT_EQ("io_added", type_str);
    EXPECT_EQ(0, ev.size());
}

/*******************************************************************************
 * THE PARSE GUARD
 ******************************************************************************/

TEST(ScriptWire, AMalformedMessageIsRefused)
{
    EXPECT_FALSE(wireAcceptMessage("{\"msg\":"));
    EXPECT_FALSE(wireAcceptMessage(""));
    EXPECT_FALSE(wireAcceptMessage("{'msg': 'execute'}")); //single quotes are not JSON
}

//json_loads() without JSON_DECODE_ANY refuses a top level scalar outright, and
//a top level array parses but fails json_is_object(). Both end in the same
//"Error parsing json from sub process" branch.
TEST(ScriptWire, ATopLevelArrayOrScalarIsRefused)
{
    EXPECT_FALSE(wireAcceptMessage("[1,2]"));
    EXPECT_FALSE(wireAcceptMessage("42"));
    EXPECT_FALSE(wireAcceptMessage("\"execute\""));
    EXPECT_FALSE(wireAcceptMessage("null"));

    EXPECT_TRUE(wireAcceptMessage("{}"));
    EXPECT_TRUE(wireAcceptMessage(FX_EXECUTE));
}

//and the decoders answer false on exactly the same inputs
TEST(ScriptWire, TheDecodersRefuseWhatTheParseGuardRefuses)
{
    string script;
    vector<Params> ios;
    Params env;
    Params ev;
    string type_str;

    EXPECT_FALSE(wireDecodeExecute("[1,2]", script, ios, env));
    EXPECT_FALSE(wireDecodeExecute("{\"msg\":", script, ios, env));
    EXPECT_FALSE(wireDecodeEvent("[1,2]", ev, type_str));
    EXPECT_FALSE(wireDecodeEvent("nope", ev, type_str));
}

/*******************************************************************************
 * THE DEFAULT CONTRACT of jansson_string_get()
 *
 * `j["k"].get<string>()` THROWS when the key is absent AND when the value is
 * not a string; `j.value("k", def)` still throws on a type mismatch. Neither
 * is a translation of this contract, and ScriptExec.cpp:81 - out of perimeter,
 * still jansson - passes a NON EMPTY default that depends on it.
 ******************************************************************************/

TEST(ScriptWire, StringGetAnswersTheDefaultWhenTheKeyIsAbsent)
{
    EXPECT_NO_THROW({
        EXPECT_EQ("", wireStringGet("{\"msg\":\"event\"}", "script", ""));
    });
}

TEST(ScriptWire, StringGetAnswersTheDefaultWhenTheValueIsNotAString)
{
    const char doc[] = "{\"n\":42,\"b\":true,\"z\":null,\"o\":{\"a\":1},\"a\":[1,2]}";

    EXPECT_NO_THROW({
        EXPECT_EQ("", wireStringGet(doc, "n", ""));
        EXPECT_EQ("", wireStringGet(doc, "b", ""));
        EXPECT_EQ("", wireStringGet(doc, "z", ""));
        EXPECT_EQ("", wireStringGet(doc, "o", ""));
        EXPECT_EQ("", wireStringGet(doc, "a", ""));
    });
}

/*
 * The NON EMPTY default, which is the form ScriptExec.cpp:81 uses. The default
 * and the key are deliberately different strings here: a swap of the two
 * arguments at the call site would answer "return_val" instead of "false".
 */
TEST(ScriptWire, StringGetHonoursANonEmptyDefault)
{
    EXPECT_EQ("false", wireStringGet("{\"msg\":\"finished\"}", "return_val", "false"));
    EXPECT_EQ("false", wireStringGet("{\"return_val\":7}", "return_val", "false"));
    EXPECT_EQ("true", wireStringGet("{\"return_val\":\"true\"}", "return_val", "false"));
}

//A non object root: the default comes back, no throw.
TEST(ScriptWire, StringGetAnswersTheDefaultOnANonObjectRoot)
{
    EXPECT_NO_THROW({
        EXPECT_EQ("dflt", wireStringGet("[1,2]", "msg", "dflt"));
        EXPECT_EQ("dflt", wireStringGet("nope", "msg", "dflt"));
    });
}

/*******************************************************************************
 * THE FLATTENING CONTRACT of jansson_decode_object()
 ******************************************************************************/

/*
 * A string as is, a boolean as the WORD, a number through
 * Utils::to_string(double), and ANY OTHER TYPE the EMPTY STRING - WITH THE KEY
 * STILL ADDED. That last clause is the one that is easy to lose: an absent key
 * and a key at "" are not the same thing downstream, ev["id"] being the
 * obvious case.
 */
TEST(ScriptWire, FlatteningTurnsEveryValueIntoAStringAndKeepsEveryKey)
{
    Params p;
    wireDecodeObject("{\"s\":\"text\",\"b_true\":true,\"b_false\":false,"
                     "\"n_int\":42,\"n_real\":18.5,"
                     "\"z\":null,\"o\":{\"a\":1},\"arr\":[1,2]}", p);

    EXPECT_EQ(8, p.size()) << "a key was dropped instead of being added empty";

    EXPECT_EQ("text", p["s"]);
    EXPECT_EQ("true", p["b_true"]);
    EXPECT_EQ("false", p["b_false"]);
    EXPECT_EQ("42", p["n_int"]);
    EXPECT_EQ("18.5", p["n_real"]);

    //present, and empty - not absent
    EXPECT_TRUE(p.Exists("z"));
    EXPECT_TRUE(p.Exists("o"));
    EXPECT_TRUE(p.Exists("arr"));
    EXPECT_EQ("", p["z"]);
    EXPECT_EQ("", p["o"]);
    EXPECT_EQ("", p["arr"]);

    //and an absent key is NOT the same thing
    EXPECT_FALSE(p.Exists("never_there"));
}

//Numbers go through Utils::to_string(double), a bare ostringstream: six
//significant digits, then scientific notation. Frozen, not fixed.
TEST(ScriptWire, FlatteningANumberGoesThroughUtilsToStringDouble)
{
    Params p;
    wireDecodeObject("{\"a\":1234.56789,\"b\":123456789.0,\"c\":7}", p);

    EXPECT_EQ("1234.57", p["a"]);
    EXPECT_EQ("1.23457e+08", p["b"]);
    EXPECT_EQ("7", p["c"]);
}

//A null root, or a non object root: no iteration, no throw, nothing added.
TEST(ScriptWire, FlatteningANonObjectAddsNothing)
{
    Params p;
    EXPECT_NO_THROW(wireDecodeObject("[1,2]", p));
    EXPECT_NO_THROW(wireDecodeObject("nope", p));
    EXPECT_NO_THROW(wireDecodeObject("null", p));
    EXPECT_EQ(0, p.size());
}

/*
 * TRIPWIRE - Params::fromJson() is NOT a substitute for this contract, and
 * this case is what stops a later reader from "simplifying" the hand written
 * decoder into it. fromJson() assigns the json value straight into a
 * std::string, which throws type_error.302 on anything that is not a JSON
 * string - and the real context of an "execute" message carries a boolean and
 * a number on EVERY IO.
 */
TEST(ScriptWire, Tripwire_ParamsFromJsonWouldThrowOnARealContextEntry)
{
    const Json io = Json::parse("{\"id\":\"io_kitchen_light\",\"state\":true,\"delay\":18.5}",
                                nullptr, false);
    ASSERT_FALSE(io.is_discarded());

    EXPECT_THROW(Params::fromJson(io), Json::type_error);

    //while the wire decoder swallows exactly the same document
    Params p;
    EXPECT_NO_THROW(wireDecodeObject("{\"id\":\"io_kitchen_light\",\"state\":true,\"delay\":18.5}", p));
    EXPECT_EQ("true", p["state"]);
    EXPECT_EQ("18.5", p["delay"]);
}
