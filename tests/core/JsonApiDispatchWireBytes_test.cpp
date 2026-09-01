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
 * E4.1s - THE INPUT PARSE AND THE LAST EMITTERS. The ticket that MOVES THE WIRE.
 *
 * ---------------------------------------------------------------------------
 * WHY THIS FILE EXISTS, AND WHY IT MATTERS MORE HERE THAN ANYWHERE ELSE
 * ---------------------------------------------------------------------------
 * The 145 goldens of E4.0 compare PARSED DOCUMENTS. They are blind, BY
 * CONSTRUCTION, to key order and to escaping. Every ticket of this series has
 * said so; E4.1s is the one where it stops being a caveat and becomes the whole
 * problem, because E4.1s is the ticket that CHANGES THE BYTES OF THE WIRE:
 *
 *   - it deletes the last two jansson emitters of the API
 *     (JsonApiHandlerHttp::sendJson(json_t *) and
 *      JsonApiHandlerWS::sendJson(const string &, json_t *, const string &)),
 *   - and it moves the REQUEST PARSE of both transports from json_loads() to
 *     Json::parse().
 *
 * A green `make check` therefore proves nothing at all about this ticket. This
 * file and the tripwire of tests/ParamsJson_test.cpp are the entire net on the
 * byte dimension, and this file is the only one that can see the INPUT surface.
 *
 * ---------------------------------------------------------------------------
 * THE CHARACTERIZATION COMMIT IS GREEN, AND THE ...Today SUFFIX SAYS WHY
 * ---------------------------------------------------------------------------
 * Written against the UNTOUCHED tree first. Every case whose name ends in
 * `Today` pins what the JANSSON tree does and is expected to be REWRITTEN by
 * the migration commit; every other case is an INVARIANT that must survive it.
 * A prediction is not a proof: the red round of the migration commit is, and
 * its measured result is written in docs/refactoring/E4.1s.md.
 *
 * ---------------------------------------------------------------------------
 * SIX ORACLES, DELIBERATELY DISJOINT (the discipline of E4.1b, l, m, r)
 * ---------------------------------------------------------------------------
 * So that a counter-mutation names ONE guilty parameter instead of reddening
 * the whole file:
 *
 *   P_  THE INPUT PARSE. What the request parser ACCEPTS and REFUSES. This
 *       oracle is NEW with this ticket and exists nowhere else in the suite:
 *       json_loads() and Json::parse() do not draw the same line, and the
 *       difference is a change of ATTACK SURFACE, not of formatting. No case
 *       here asserts an escaping or a key order.
 *
 *   N_  THE EMBEDDED NUL. Its own oracle, and not a sub-case of P_, because
 *       the NUL is the one input the parse bascule makes REACHABLE FOR THE
 *       FIRST TIME. See the block below.
 *
 *   K_  KEY ORDER. ASCII only payloads, so no escaping question can reach
 *       them. jansson walks an object in INSERTION order; nlohmann::json is a
 *       std::map and walks it SORTED.
 *
 *   A_  ensure_ascii ONLY. The probe is a VALID code point (U+00E9), so no
 *       error handler ever looks at it and no case here asserts an order.
 *       jansson's JSON_ENSURE_ASCII writes an UPPERCASE hexadecimal, nlohmann
 *       writes a lowercase one. THIS IS THE FORM 3 BASCULE.
 *
 *   D_  U+007F (DEL). Neither library calls it a control character, but
 *       nlohmann escapes it under ensure_ascii and jansson does not. It is the
 *       only one of the deltas that changes the LENGTH of the payload, hence
 *       the HTTP Content-Length.
 *
 *   R_  error_handler_t::replace ONLY. The probe is a byte pair that can never
 *       become valid UTF-8. On the jansson tree THE WHOLE PAIR DISAPPEARS -
 *       json_string() answers NULL, json_object_set_new() then returns -1, and
 *       processConfig() tests NEITHER return code.
 *
 *   W_  WITNESSES, green on BOTH trees. Contracts the migration must not
 *       break: the omitted "data" member, the upload whitelist, "false" as a
 *       JSON string and never a JSON boolean, Content-Length following the
 *       body, and no JSON null anywhere.
 *
 * ---------------------------------------------------------------------------
 * ⛔⭐⭐ THE MINE: THE EMBEDDED NUL, AND WHY IT IS THIS TICKET'S CHARGE
 * ---------------------------------------------------------------------------
 * MEASURED on a probe compiled against the real jansson and the repository's
 * json.hpp (3.11.3), and it is the reason the N_ oracle exists:
 *
 *   - json_loads() REFUSES an ESCAPED NUL outright, in a value
 *     ("\u0000 is not allowed without JSON_ALLOW_NUL") and in a key ("NUL byte
 *     in object key not supported"). The whole request dies before any
 *     dispatch runs.
 *   - Json::parse() ACCEPTS it and hands down a std::string that really holds
 *     the zero byte.
 *
 * ⇒ E4.1s OPENS A PATH THAT DOES NOT EXIST TODAY. It is not a regression
 *   introduced by clumsiness: it is a charge that was already armed, and this
 *   ticket is its detonator. The N_ cases below make it FALSIFIABLE instead of
 *   theoretical: they push a NUL through the parse and pin what comes out.
 *
 * ⚠️ A RAW NUL IS NOT THE SAME QUESTION, and the measurement contradicts the
 *   intuition: nlohmann's lexer treats the byte 0x00 as END OF INPUT (json.hpp
 *   spells it out: "the null byte is needed when parsing from string
 *   literals"), exactly as json_loads(data.c_str()) did. A raw NUL in the body
 *   therefore still truncates the request on BOTH sides, and only the ESCAPED
 *   form is new. P_ARawNulStillEndsTheBodyOnBothParsers pins that, so that
 *   "the NUL got through" can never be confused with "the body got truncated".
 *
 * ⛔ WHERE THE NUL STILL DIES, AND WHOSE TICKET THAT IS. Scenario::toJson()
 *   lives in IO/Scenario.cpp, EXCLUDED from E4.1 by decision Q5, and still
 *   builds its strings with json_string(const char *): a NUL in a scenario
 *   ACTION truncates the value on the way out. That case is pinned where it
 *   belongs, in core/JsonApiScenarioWireBytes_test.cpp, and it is E4.6d's to
 *   fix - NOT this ticket's. This file pins the other half: through the
 *   emitters E4.1s owns, the NUL travels WHOLE, as \u0000.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE FORM 3 BASCULE IS ACTUALLY OBSERVABLE, MEASURED
 * ---------------------------------------------------------------------------
 * After E4.1r, almost every payload of the API is ALREADY emitted by the
 * nlohmann overload of sendJson(): the escaping bascule happened for them in
 * E4.1b/l/m/n/o/p/q/r. The emitters this ticket is the last to move are:
 *
 *   HTTP  get_mcp_info, the four get_cover/get_camera_pic error payloads,
 *         exeFinished()'s two payloads, the audio get_cover error payloads,
 *         and processConfig().
 *   WS    the login refusal payload, and the "no data" envelope.
 *
 * ⇒ Of those, EXACTLY ONE carries bytes a client can influence: processConfig
 *   "get", which hands back the raw text of io.xml / rules.xml /
 *   local_config.xml. It is also the biggest payload of the API and the most
 *   serious candidate for a type_error.316. It is therefore the carrier of the
 *   A_, D_ and R_ probes AND of the wire tripwire below. Everything else in
 *   the perimeter is a constant ASCII payload and can only carry a K_ oracle.
 *   That asymmetry is a measurement, not a shortcut: there is no form 1
 *   emitter left on the websocket that carries client data at all.
 *
 * ---------------------------------------------------------------------------
 * ANTI "FIXTURE PAUVRE" (relapses in this series, all found by review)
 * ---------------------------------------------------------------------------
 *   - the three config_files keys are asked in an order whose ALPHABETICAL
 *     sort is NOT their insertion order (insertion io.xml, rules.xml,
 *     local_config.xml; sorted io.xml, local_config.xml, rules.xml): two of
 *     the three positions move, so no partial coincidence passes this;
 *   - get_mcp_info's three keys are likewise inserted url_path, token, hint
 *     and sort hint, token, url_path - every one of the three positions moves;
 *   - the get_cover refusal carries success THEN error_str, which sorts the
 *     other way round;
 *   - every probe is planted on ONE file of the three, and the case asserts
 *     that the OTHER TWO are untouched, so "escapes everything" cannot pass
 *     for "escapes the right thing";
 *   - each probe is asserted to be PRESENT ON DISK before the request is sent.
 *     A probe the XML writer silently dropped would otherwise make the case
 *     pass while measuring nothing;
 *   - the invalid-UTF-8 probe lives in a case of its own: on the jansson tree
 *     it makes the WHOLE io.xml pair vanish, which would wipe out every other
 *     assertion sharing the fixture.
 *
 * This file adds NO GOLDEN. tests/core/golden must keep the tree hash it has.
 ******************************************************************************/

#include "JsonApiCharacterization.h"

#include "ListeRoom.h"
#include "IOBase.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/*******************************************************************************
 * The byte shapes, spelled in escapes so that no editor and no locale can
 * rewrite them. NOTHING here is case folded: case folding is exactly the defect
 * E4.1a found in the ParamsJson tripwire, which is why it had to be rewritten.
 ******************************************************************************/
const char *const ASCII_E_LOWER = "\\u00e9";   //form 3: nlohmann, ensure_ascii
const char *const ASCII_E_UPPER = "\\u00E9";   //form 1: jansson, JSON_ENSURE_ASCII
const char *const ASCII_DEL     = "\\u007f";
const char *const ASCII_NUL     = "\\u0000";
const char *const ASCII_US_LOW  = "\\u001f";   //U+001F, form 2 and 3
const char *const ASCII_US_UP   = "\\u001F";   //U+001F, form 1
const char *const ASCII_SOH     = "\\u0001";   //U+0001: IDENTICAL in all three

//The raw UTF-8 of U+00E9. Form 2 - a bare dump() - is the only one that puts
//these two bytes on the wire, and form 2 is a FAILURE of this ticket, not a
//variant of it.
const std::string RAW_E = "\xc3\xa9";

//0xFF is not a legal lead byte in any position and 0x80 is a continuation byte
//with nothing to continue: this pair can never become valid UTF-8 by accident.
const std::string INVALID_UTF8 = std::string("\xff\x80", 2);

//What U+FFFD looks like once the wire has been parsed back: one replacement
//character PER bad byte, so this pair answers two.
const std::string TWO_REPLACEMENTS = "\xef\xbf\xbd\xef\xbf\xbd";

//A value with an embedded NUL. Built with an explicit length: a plain string
//literal would stop at the NUL and the probe would BE the truncation it is
//meant to detect.
const std::string NUL_INSIDE = std::string("a\0b", 3);

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

//"the key IS THERE", which is a different question from "its value is empty"
//and from "its value is null". The R_ oracle lives on exactly that difference.
bool has(const Json &j, const char *key)
{
    return j.is_object() && j.find(key) != j.cend();
}

bool hasAnyByteAbove7f(const std::string &s)
{
    for (unsigned char c: s)
        if (c >= 0x80)
            return true;
    return false;
}

/*******************************************************************************
 * Reading the DOCUMENT ORDER of the wire.
 *
 * nlohmann::ordered_json keeps the order the text was written in, so a case can
 * ask "which key came first" without hunting substrings. This is a READER, in
 * the test only: production emits plain nlohmann::json and must keep doing so
 * (invariant 1 of the epic).
 ******************************************************************************/
nlohmann::ordered_json ordered(const std::string &wire)
{
    return nlohmann::ordered_json::parse(wire, nullptr, false);
}

std::vector<std::string> keyOrder(const nlohmann::ordered_json &j)
{
    std::vector<std::string> keys;
    if (j.is_object())
        for (auto it = j.begin(); it != j.end(); ++it)
            keys.push_back(it.key());
    return keys;
}

std::string joined(const std::vector<std::string> &keys)
{
    std::string s;
    for (const std::string &k: keys)
        s += (s.empty()? "": ",") + k;
    return s;
}

//A JSON document as raw TEXT, assembled by hand. Every P_ case needs a body
//that one of the two parsers refuses, so none of them can be built by dumping
//a Json - the dump would produce a document both parsers accept.
std::string nested(int depth)
{
    std::string s;
    s.reserve(2 * depth);
    for (int i = 0; i < depth; i++) s += '[';
    for (int i = 0; i < depth; i++) s += ']';
    return s;
}

} //namespace

class JsonApiDispatchWireBytesTest: public JsonApiCharacterizationTest
{
protected:
    /* ------------------------------------------------------------------
     * Requests. HTTP puts everything at the root and needs credentials; WS
     * puts the sub-command under "data" and is authenticated by the harness.
     * --------------------------------------------------------------- */

    //A RAW HTTP body, credentials spelled in by hand. Needed by every P_ case:
    //the probe has to survive as TEXT, and authenticated(Json) would dump it.
    static std::string httpRawBody(const std::string &members)
    {
        return std::string("{\"cn_user\":\"") + apiUser() +
               "\",\"cn_pass\":\"" + apiPassword() + "\"" +
               (members.empty()? std::string(): "," + members) + "}";
    }

    /* ⚠️ EVERY HTTP request of this file clears the login throttle first, and
     * that is not hygiene, it is an ORACLE PROBLEM the red round of this ticket
     * CAUGHT RED HANDED.
     *
     * A refused request registers a login FAILURE, and the next request from
     * the same address inside the backoff window answers 400 whatever its body
     * says. P_ANestingDepthAbove2048... was written as "a refusal, then the
     * probe" and stayed GREEN through the migration - not because the parser
     * still refused the deep document, but because the FIRST half had poisoned
     * the throttle and the second half could not have answered anything else.
     * The "fixture pauvre" trap in its nastiest form: a case that passes for a
     * reason that has nothing to do with what it claims to measure.
     *
     * The throttle has a suite of its own (core/JsonApiThrottleIdentity_test);
     * it is not this file's oracle, so it is neutralised here.
     */
    static void clearThrottle() { LoginThrottle::clear(); }

    //Answers the status line of one HTTP request driven on a clean throttle.
    static std::string httpStatusFor(const std::string &rawBody)
    {
        clearThrottle();
        HttpTestRequest req;
        req.send(rawBody);
        EXPECT_EQ(1u, req.count());
        return req.statusLine();
    }

    //Drives one HTTP request and answers the raw body of the response.
    static std::string httpWire(const Json &body)
    {
        clearThrottle();
        HttpTestRequest req;
        req.send(authenticated(body));
        return req.body();
    }

    //Plants a probe on the NAME of one IO of the reference house. Everything
    //an IO carries in its params is written back into io.xml by
    //Config::SaveConfigIO(), which processConfig("get") calls before reading
    //the file - so this is how a client-influenced byte reaches the biggest
    //payload of the API.
    static void probeIoName(const char *ioId, const std::string &value)
    {
        IOBase *io = ListeRoom::Instance().get_io(ioId);
        ASSERT_TRUE(io != nullptr) << "the reference house lost " << ioId;
        io->set_param("name", value);
    }

    /* The whole point of the fixture, and the guard that keeps it honest.
     *
     * Answers the raw body of `config get` AFTER checking that the probe
     * really is in the io.xml on disk. Config::SaveConfigIO() goes through the
     * XML writer, and a writer that dropped or escaped the probe would leave
     * every assertion below asserting nothing at all - the "fixture pauvre"
     * trap, seven relapses in E4.0.
     */
    std::string configGetWireWithProbeOnDisk(const std::string &probe)
    {
        clearThrottle();
        HttpTestRequest req;
        req.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));

        EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());

        const std::string onDisk = ioXmlOnDisk();
        EXPECT_NE(std::string::npos, onDisk.find(probe))
                << "the probe never reached io.xml: the XML writer dropped or "
                   "rewrote it, and this case would be measuring nothing";

        return req.body();
    }
};

/*******************************************************************************
 * P_ - THE INPUT PARSE. What the request parser accepts and refuses.
 *
 * This is the oracle that is NEW with this ticket. No case here asserts an
 * escaping or a key order.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, P_AnEscapedNulInAValueNowTraversesTheParser)
{
    /* ⛔⭐⭐ THE MINE, seen from the door it comes through, AFTER E4.1s opened
     * it. This case was P_AnEscapedNulInAValueIsRefusedByTheParserToday and it
     * pinned the refusal:
     *
     *   json_loads() answered "\u0000 is not allowed without JSON_ALLOW_NUL"
     *   and threw the whole document away. On HTTP the handler fell back to
     *   the (empty) GET parameters and answered 400 on the credentials; on the
     *   websocket the message was dropped without a word.
     *
     * Json::parse() accepts the very same bytes. The request is now SERVED on
     * both transports. This is not a formatting delta: it is a change of
     * INPUT SURFACE, declared in RELEASE_NOTES.md, and it is the reason the
     * truncation still living inside Scenario::toJson() (IO/Scenario.cpp,
     * EXCLUDED by Q5, rewritten by E4.6d) becomes reachable for the first
     * time. The case that pins that truncation is in
     * core/JsonApiScenarioWireBytes_test.cpp.
     */
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody("\"action\":\"get_home\","
                                        "\"probe\":\"a\\u0000b\"")))
            << "the HTTP parser went back to refusing an escaped NUL";

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\","
                        "\"probe\":\"a\\u0000b\"}"));
    pumpEventLoop();
    EXPECT_EQ(1u, ws.count())
            << "the WS parser went back to refusing an escaped NUL";
}

TEST_F(JsonApiDispatchWireBytesTest, P_AnEscapedNulInAKeyNowTraversesTheParser)
{
    //The other half, and jansson had a SECOND, DIFFERENT refusal for it ("NUL
    //byte in object key not supported"). Both refusals went away together,
    //which is why they are pinned separately: a guard put back on values only
    //would leave this case green.
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody("\"action\":\"get_home\","
                                        "\"a\\u0000b\":\"v\"")))
            << "the HTTP parser went back to refusing an escaped NUL in a key";

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\","
                        "\"a\\u0000b\":\"v\"}"));
    pumpEventLoop();
    EXPECT_EQ(1u, ws.count())
            << "the WS parser went back to refusing an escaped NUL in a key";
}

TEST_F(JsonApiDispatchWireBytesTest, P_AnIntegerBeyondInt64NowTraversesTheParser)
{
    /* The SECOND relaxation of the input surface, and it arrives with the same
     * line of code as the NUL. jansson answered "too big integer" and threw the
     * WHOLE document away; nlohmann parses the literal into a double
     * (1.2345678901234568e+29) and carries on. Downstream the flattening
     * contract turns it into a string through Utils::to_string(double), a bare
     * ostringstream frozen on purpose - so what used to be a 400 is now a
     * served request. Declared in RELEASE_NOTES.md.
     */
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody("\"action\":\"get_home\","
                                        "\"probe\":123456789012345678901234567890")))
            << "the HTTP parser went back to refusing an integer beyond int64";

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\","
                        "\"probe\":123456789012345678901234567890}"));
    pumpEventLoop();
    EXPECT_EQ(1u, ws.count())
            << "the WS parser went back to refusing an integer beyond int64";
}

TEST_F(JsonApiDispatchWireBytesTest, P_ANestingDepthAbove2048NowTraversesTheParser)
{
    /* ⚠️ CORRECTS THE TICKET SHEET, WHICH SAYS "jansson has no default limit".
     * MEASURED: jansson caps nesting at 2048 (JSON_PARSER_MAX_DEPTH) and
     * answers "maximum parsing depth reached"; nlohmann has NO limit at all,
     * which is the THIRD relaxation of the input surface.
     *
     * ⭐ AND IT IS NOT A NEW DENIAL OF SERVICE, measured rather than assumed:
     * 100000 levels parse AND DESTRUCT without a stack overflow (json.hpp
     * 3.11.3 destroys iteratively), and the body is still bounded by the HTTP
     * request size. What changed is what is ACCEPTED, not whether the process
     * survives.
     *
     * ⛔ THIS CASE USED TO PASS FOR THE WRONG REASON, and the red round of the
     * migration is what exposed it. It sent a refusal first and the probe
     * second, so the LOGIN THROTTLE - not the parser - answered 400 to the
     * second one, and the case stayed green through a bascule that had changed
     * exactly what it claimed to measure. Every HTTP request of this file now
     * goes through httpStatusFor(), which clears the throttle. See the comment
     * on clearThrottle().
     */
    loadReferenceHouse();

    //2048 levels: accepted by BOTH parsers, so the document reaches the
    //dispatch and dies on "not an object" - the GET fallback, hence 400. The
    //INVARIANT half, and the contrast that says the case below moved on the
    //DEPTH and not on something else.
    EXPECT_EQ("HTTP/1.0 400 Bad Request", httpStatusFor(nested(2048)));

    //A depth of 2050 inside a real object: refused by jansson, served now.
    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody("\"action\":\"get_home\",\"probe\":" +
                                        nested(2049))))
            << "the HTTP parser went back to refusing a nesting depth above 2048";
}

TEST_F(JsonApiDispatchWireBytesTest, P_InvalidUtf8InTheBodyIsRefusedByBothParsers)
{
    /* ⛔ INVARIANT, AND THE ONE GUARD THAT MUST NOT MOVE. The request parse is
     * the last lock on the input side: json_loads() refuses invalid UTF-8
     * ("unable to decode byte 0xff") and Json::parse() refuses it too.
     * MEASURED on both. If this case ever needs its assertion changed, a guard
     * has been released - stop.
     */
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 400 Bad Request",
              httpStatusFor(httpRawBody("\"action\":\"get_home\",\"probe\":\"" +
                                        INVALID_UTF8 + "\"")))
            << "a parser started accepting invalid UTF-8 in the request body";

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"get_home\",\"msg_id\":\"1\",\"probe\":\"") +
            INVALID_UTF8 + "\"}");
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count())
            << "the WS parser started accepting invalid UTF-8";
}

TEST_F(JsonApiDispatchWireBytesTest, P_ALoneSurrogateIsRefusedByBothParsers)
{
    //INVARIANT. \ud800 without its low half is the escaped spelling of the same
    //defect: a code point that cannot be encoded. Both parsers refuse it, and
    //that is the other half of the UTF-8 lock.
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 400 Bad Request",
              httpStatusFor(httpRawBody("\"action\":\"get_home\",\"probe\":\"\\ud800\"")))
            << "a parser started accepting a lone surrogate";
}

TEST_F(JsonApiDispatchWireBytesTest, P_ARealNumberOverflowIsRefusedByBothParsers)
{
    //INVARIANT, and the CONTRAST that makes the big-integer case above mean
    //something: nlohmann is not simply more permissive on numbers. 1e400
    //overflows a double and BOTH parsers throw the document away. Asserted in
    //both signs, because a parser that only checked the positive side would
    //otherwise pass.
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 400 Bad Request",
              httpStatusFor(httpRawBody("\"action\":\"get_home\",\"probe\":1e400")))
            << "a parser started accepting a real number overflow";

    EXPECT_EQ("HTTP/1.0 400 Bad Request",
              httpStatusFor(httpRawBody("\"action\":\"get_home\",\"probe\":-1e400")))
            << "a parser started accepting a negative real number overflow";
}

TEST_F(JsonApiDispatchWireBytesTest, P_TrailingGarbageIsRefusedByBothParsers)
{
    //INVARIANT. Both parsers demand end-of-input after the document; neither
    //serves the prefix and ignores the rest.
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 400 Bad Request",
              httpStatusFor(httpRawBody("\"action\":\"get_home\"") + " trailing"))
            << "a parser started serving a document with trailing garbage";
}

TEST_F(JsonApiDispatchWireBytesTest, P_ARawNulStillEndsTheBodyOnBothParsers)
{
    /* ⭐ THE MEASUREMENT THAT KILLS THE OBVIOUS GUESS, and the reason the N_
     * oracle is about the ESCAPED NUL and nothing else.
     *
     * json_loads(data.c_str()) stopped at the first zero byte. nlohmann's
     * LEXER does the same thing for its own reason - json.hpp lists '\0' next
     * to eof() in scan(), commented "the null byte is needed when parsing from
     * string literals". So:
     *
     *   - a raw NUL AFTER a complete document leaves the document parsable on
     *     both sides: the trailing bytes are invisible;
     *   - a raw NUL INSIDE a string truncates the input on both sides and the
     *     document is refused as unterminated.
     *
     * Both halves are asserted, because "the NUL got through" and "the body got
     * truncated" are two different answers and only the first is this ticket's.
     */
    loadReferenceHouse();

    //(a) after a complete document: served, trailing bytes ignored, BOTH sides.
    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody("\"action\":\"get_home\"") +
                            std::string("\0garbage", 8)))
            << "a raw NUL after the document stopped being invisible";

    //(b) inside a string: the input ends there, the document is unterminated.
    //Spelled out by hand, brace included: httpRawBody() would close the object
    //BEFORE the probe and the case would measure a malformed document instead.
    const std::string truncated = std::string("{\"cn_user\":\"") + apiUser() +
                                  "\",\"cn_pass\":\"" + apiPassword() +
                                  "\",\"action\":\"get_home\",\"probe\":\"x" +
                                  std::string("\0y\"}", 4);

    EXPECT_EQ("HTTP/1.0 400 Bad Request", httpStatusFor(truncated))
            << "a raw NUL inside a string stopped truncating the body";
}

TEST_F(JsonApiDispatchWireBytesTest, P_ANonObjectBodyIsStillNoJsonAtAll)
{
    //INVARIANT. A perfectly valid JSON array is NOT a request: both transports
    //treat "parsed but not an object" exactly as "did not parse". The check is
    //`is_object()` on one side and `json_is_object()` on the other, and it must
    //stay an OBJECT check and not become a "did it parse" check.
    loadReferenceHouse();

    EXPECT_EQ("HTTP/1.0 400 Bad Request", httpStatusFor(std::string("[1,2,3]")));

    WsTestSession ws;
    ws.send(std::string("[1,2,3]"));
    pumpEventLoop();
    EXPECT_EQ(0u, ws.count());
}

TEST_F(JsonApiDispatchWireBytesTest, P_TheGetParameterFallbackStillServesTheRequest)
{
    //INVARIANT, and the CONTRAST for every P_ refusal above: an unparsable body
    //is not an error in itself on HTTP, it is the GET-parameter path. Without
    //this case, "400 Bad Request" everywhere above could just as well mean the
    //transport is broken.
    loadReferenceHouse();

    Params get;
    get.Add("cn_user", apiUser());
    get.Add("cn_pass", apiPassword());
    get.Add("action", "get_home");

    clearThrottle();
    HttpTestRequest req;
    req.send(std::string("this is not json"), get);
    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine());
    EXPECT_TRUE(contains(req.body(), "\"home\""));
}

/*******************************************************************************
 * N_ - THE EMBEDDED NUL, once it is through the door.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, N_AnEscapedNulInAParamValueIsStoredWholeAndComesBackEscaped)
{
    /* ⛔⭐⭐ THE MINE, END TO END, on the shortest path that ECHOES the value
     * back: set_param stores it on the IO, get_param reads it out again.
     *
     * This case was N_AnEscapedNulInAParamValueIsRefusedToday and it pinned the
     * refusal: the request died in the parser and NOTHING was written.
     *
     * THE MEASURED VERDICT, and it is the one E4.1s owns: through the emitters
     * this ticket leaves behind, the NUL travels WHOLE. Three bytes are stored,
     * three bytes come back, and the wire spells the zero byte \u0000 - it is
     * not truncated, not dropped, not replaced. THE DECISION IS TO ACCEPT IT
     * and to say so: no guard is added, because a guard would be a new refusal
     * this ticket was not asked to invent, and because the place where the NUL
     * still does damage is Scenario::toJson(), inside the file E4.1 excludes.
     *
     * Every assertion is on the length as well as on the content: "a" and
     * "a\0b" compare EQUAL through a const char *, which is exactly the
     * confusion this case exists to prevent.
     */
    loadReferenceHouse();

    IOBase *io = ListeRoom::Instance().get_io(HOUSE_STRING);
    ASSERT_TRUE(io != nullptr);

    EXPECT_EQ("HTTP/1.0 200 OK",
              httpStatusFor(httpRawBody(std::string("\"action\":\"set_param\",\"id\":\"") +
                                        HOUSE_STRING + "\",\"param\":\"e41s_nul\","
                                        "\"value\":\"a\\u0000b\"")))
            << "set_param went back to refusing an escaped NUL";

    //Stored WHOLE: three bytes, not the one byte a C string would have kept.
    ASSERT_TRUE(io->get_params().Exists("e41s_nul"));
    EXPECT_EQ(NUL_INSIDE, io->get_param("e41s_nul"));
    EXPECT_EQ(3u, io->get_param("e41s_nul").size())
            << "the value was truncated at the zero byte on the way in";

    //And back out, on the RAW wire, as the escape - never as a raw zero byte
    //and never truncated.
    clearThrottle();
    HttpTestRequest back;
    back.send(authenticated(Json{{ "action", "get_param" },
                                 { "id", HOUSE_STRING },
                                 { "param", "e41s_nul" }}));
    ASSERT_EQ(1u, back.count());
    EXPECT_TRUE(contains(back.body(), std::string("\"a") + ASCII_NUL + "b\""))
            << back.body();
    EXPECT_EQ(std::string::npos, back.body().find('\0'))
            << "a raw zero byte reached the wire";
    EXPECT_EQ(NUL_INSIDE, str(back.bodyJson(), "e41s_nul"));
}

TEST_F(JsonApiDispatchWireBytesTest, N_AnEscapedNulInAWsRequestIsStoredWholeAndComesBackEscaped)
{
    //The websocket half of the same door. Kept separate from the HTTP one
    //because the two transports parse in two different functions and a
    //migration that forgot one of them would leave the other case green.
    loadReferenceHouse();

    IOBase *io = ListeRoom::Instance().get_io(HOUSE_STRING);
    ASSERT_TRUE(io != nullptr);

    WsTestSession ws;
    ws.send(std::string("{\"msg\":\"set_param\",\"msg_id\":\"1\",\"data\":{\"id\":\"") +
            HOUSE_STRING + "\",\"param\":\"e41s_wsnul\",\"value\":\"a\\u0000b\"}}");
    pumpEventLoop();

    /* TWO messages, and the second is not noise: set_param raises an
     * EventIOChanged, this session is subscribed to it since its constructor,
     * and the event carries the value. So the NUL reaches the EVENT wire as
     * well as the answer - one more emitter, and it is asserted below rather
     * than tolerated. On the jansson tree this count was ZERO: the message
     * died in the parser and no event was ever raised.
     */
    ASSERT_EQ(2u, ws.count()) << "the WS set_param went back to refusing a NUL";
    EXPECT_TRUE(contains(ws.lastMessage(), std::string("\"a") + ASCII_NUL + "b\""))
            << "the event wire lost the NUL: " << ws.lastMessage();

    ASSERT_TRUE(io->get_params().Exists("e41s_wsnul"));
    EXPECT_EQ(NUL_INSIDE, io->get_param("e41s_wsnul"));
    EXPECT_EQ(3u, io->get_param("e41s_wsnul").size());

    ws.clear();
    ws.send(Json{{ "msg", "get_param" }, { "msg_id", "2" },
                 { "data", {{ "id", HOUSE_STRING }, { "param", "e41s_wsnul" }} }});
    ASSERT_EQ(1u, ws.count());
    EXPECT_TRUE(contains(ws.lastMessage(), std::string("\"a") + ASCII_NUL + "b\""))
            << ws.lastMessage();
    EXPECT_EQ(std::string::npos, ws.lastMessage().find('\0'))
            << "a raw zero byte reached the wire";
}

/*******************************************************************************
 * K_ - KEY ORDER on the emitters this ticket is the last to move. ASCII only
 * payloads: no escaping is ever asserted here.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, K_GetMcpInfoKeysAreSorted)
{
    /* json_object_set_new() walked url_path, token, hint, in that order, and
     * jansson dumped an object in INSERTION order. Alphabetically that order is
     * hint, token, url_path - EVERY ONE of the three positions moves, and no
     * accident produces that. Declared byte delta of E4.1s.
     */
    const std::string wire = httpWire(Json{{ "action", "get_mcp_info" }});

    const nlohmann::ordered_json doc = ordered(wire);
    ASSERT_FALSE(doc.is_discarded()) << wire;

    const std::vector<std::string> keys = keyOrder(doc);
    ASSERT_EQ(3u, keys.size()) << joined(keys);
    EXPECT_EQ("hint,token,url_path", joined(keys));
}

TEST_F(JsonApiDispatchWireBytesTest, K_GetCoverRefusalKeysAreSorted)
{
    //processGetCover() sets success THEN error_str; sorted, that is error_str
    //THEN success. Both positions move. The payload is pure ASCII, so nothing
    //but the order can make this case red.
    loadReferenceHouse();

    const std::string wire = httpWire(Json{{ "action", "get_cover" },
                                           { "id", "e41s_no_such_player" }});

    const nlohmann::ordered_json doc = ordered(wire);
    ASSERT_FALSE(doc.is_discarded()) << wire;

    const std::vector<std::string> keys = keyOrder(doc);
    ASSERT_EQ(2u, keys.size()) << joined(keys);
    EXPECT_EQ("error_str,success", joined(keys));
}

TEST_F(JsonApiDispatchWireBytesTest, K_GetCameraPicRefusalKeysAreSorted)
{
    //The twin refusal, on the other binary operation, and a DIFFERENT call site
    //of the same shape: a migration that moved one and forgot the other would
    //leave the case above green.
    loadReferenceHouse();

    const std::string wire = httpWire(Json{{ "action", "get_camera_pic" },
                                           { "id", "e41s_no_such_camera" }});

    const nlohmann::ordered_json doc = ordered(wire);
    ASSERT_FALSE(doc.is_discarded()) << wire;
    EXPECT_EQ("error_str,success", joined(keyOrder(doc)));
}

TEST_F(JsonApiDispatchWireBytesTest, K_ConfigGetFileNamesAreSorted)
{
    /* processConfig() sets io.xml, rules.xml, local_config.xml in that order.
     * Sorted, that is io.xml, local_config.xml, rules.xml: positions two and
     * three EXCHANGE. Chosen deliberately over a set of names whose insertion
     * order already is alphabetical, which would prove nothing.
     */
    loadReferenceHouse();

    clearThrottle();
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));
    ASSERT_EQ(1u, req.count());

    const nlohmann::ordered_json doc = ordered(req.body());
    ASSERT_FALSE(doc.is_discarded()) << req.body().substr(0, 200);

    const std::vector<std::string> files = keyOrder(doc["config_files"]);
    ASSERT_EQ(3u, files.size()) << joined(files);
    EXPECT_EQ("io.xml,local_config.xml,rules.xml", joined(files));
}

/*******************************************************************************
 * A_ - ensure_ascii ONLY. THE FORM 3 BASCULE, on the one emitter of this
 * perimeter that carries bytes a client can influence.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, A_ConfigGetEscapesNonAsciiInLowercaseHex)
{
    /* ⭐ THE FORM 3 BASCULE, on the one emitter of this perimeter that carries
     * bytes a client can influence. The probe is a VALID code point, so no
     * error handler ever looks at it: this case is sensitive to ensure_ascii
     * and to NOTHING ELSE.
     *
     * processConfig() used to answer through sendJson(json_t *), which dumped
     * with JSON_COMPACT | JSON_ENSURE_ASCII and wrote an UPPERCASE
     * hexadecimal. That was FORM 1. It is FORM 3 now - lowercase, still pure
     * ASCII - and the three assertions below say so separately: form 3 IS
     * there, form 1 is NOT, and form 2 (raw UTF-8) is not either.
     */
    loadReferenceHouse();

    probeIoName(HOUSE_STRING, "caf" + RAW_E);

    const std::string wire = configGetWireWithProbeOnDisk("caf" + RAW_E);

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "processConfig() is not emitting form 3";
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER))
            << "the wire fell back to jansson's UPPERCASE hexadecimal";
    EXPECT_FALSE(contains(wire, RAW_E))
            << "raw UTF-8 bytes reached the wire - that is FORM 2, and form 2 "
               "is a failure of this ticket, not a variant of it";
    EXPECT_FALSE(hasAnyByteAbove7f(wire))
            << "the config payload stopped being pure ASCII";
}

TEST_F(JsonApiDispatchWireBytesTest, D_ConfigGetNowEscapesDel)
{
    /* U+007F is the delta that changes the LENGTH of the payload, and it is a
     * delta neither library calls a control character: jansson left it raw
     * under JSON_ENSURE_ASCII, nlohmann escapes it. Its own case, because it
     * is the only one that moves Content-Length - a three character value goes
     * from 11 bytes to 16.
     */
    loadReferenceHouse();

    const std::string probe = std::string("a\x7f" "b", 3);
    probeIoName(HOUSE_STRING, "e41s-" + probe);

    const std::string wire = configGetWireWithProbeOnDisk("e41s-" + probe);

    EXPECT_TRUE(contains(wire, ASCII_DEL))
            << "U+007F stopped being escaped";
    EXPECT_FALSE(contains(wire, probe))
            << "the raw DEL byte is back on the wire";
}

TEST_F(JsonApiDispatchWireBytesTest, R_ConfigGetNoLongerDropsTheIoXmlPairOnInvalidUtf8)
{
    /* ⛔ THE WORST OF THE FIVE DELTAS, on the biggest payload of the API.
     *
     * io.xml / rules.xml / local_config.xml are USER FILES: they can hold any
     * byte, and nothing on the way in guarantees UTF-8. json_string() answers
     * NULL on the whole file content, json_object_set_new() then returns -1,
     * and processConfig() tests NEITHER return code. So the client gets a
     * 200 whose "config_files" is MISSING io.xml entirely - not an empty
     * string, not a null: the key is gone, and the answer is indistinguishable
     * from a configuration that has no io.xml at all.
     *
     * The other two files are asserted PRESENT in the same answer: "dropped the
     * bad one" must not be confused with "dropped everything".
     *
     * error_handler_t::replace ENDS THAT: one U+FFFD per bad byte, the key
     * survives, and what the operator gets back is a configuration file that
     * is visibly mangled instead of one that is silently absent.
     *
     * ⛔ AND IT IS THE REASON error_handler_t::replace IS NOT OPTIONAL: a bare
     * dump() on this payload is type_error.316, uncaught above processApi(),
     * i.e. std::terminate on a live connection, on bytes a client can put in a
     * user file. Both halves are asserted - the answer is DELIVERED and the
     * bytes were REPLACED, not ignored - so `strict` and `ignore` cannot
     * redden the same assertion.
     *
     * Its own case, and not a probe added to the A_/D_ fixture, precisely
     * because it used to destroy the payload it travels in.
     */
    loadReferenceHouse();

    probeIoName(HOUSE_STRING, "e41s-" + INVALID_UTF8 + "-end");

    clearThrottle();
    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("HTTP/1.0 200 OK", req.statusLine())
            << "the invalid bytes killed the connection instead of the pair";
    ASSERT_TRUE(req.closes().empty())
            << "the connection was closed on an invalid-UTF-8 payload";

    ASSERT_NE(std::string::npos, ioXmlOnDisk().find(INVALID_UTF8))
            << "the probe never reached io.xml: this case measures nothing";

    const Json files = member(req.bodyJson(), "config_files");
    ASSERT_TRUE(files.is_object()) << req.body().substr(0, 200);

    EXPECT_TRUE(has(files, "io.xml"))
            << "the invalid-UTF-8 pair is being dropped again";
    EXPECT_TRUE(has(files, "rules.xml"));
    EXPECT_TRUE(has(files, "local_config.xml"));
    EXPECT_EQ(3u, files.size());

    //REPLACED, not ignored: one U+FFFD per bad byte, and both ends of the
    //probe survive - this is a substitution, not a truncation.
    const std::string io = str(files, "io.xml");
    EXPECT_NE(std::string::npos, io.find("e41s-" + TWO_REPLACEMENTS + "-end"))
            << "the bad bytes were dropped rather than replaced";
    EXPECT_EQ(std::string::npos, io.find(INVALID_UTF8))
            << "the invalid bytes reached the wire untouched";

    EXPECT_EQ("true", str(req.bodyJson(), "success"));
}

/*******************************************************************************
 * THE WIRE TRIPWIRE - which of the three bytestreams the API really emits.
 *
 * The library-level twin lives in tests/ParamsJson_test.cpp
 * (Tripwire_TheThreeWireEscapingsAreThreeDifferentBytestreams) and pins the
 * three forms against each other. THIS one is the only tripwire of the suite
 * that reads the bytes a production emitter actually put on a socket, so it is
 * the one an emitter mutation reddens.
 *
 * ⚠️ NOTHING here is case folded, in either direction. Case folding is exactly
 * the defect E4.1a found in the library tripwire, which had to be rewritten
 * because a port to ensure_ascii would have left it green.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, Tripwire_TheHttpApiWireIsFormThreeAndNeitherOfTheOtherTwo)
{
    /* The three forms, asserted SEPARATELY on the RAW body:
     *
     *   form 1  jansson + JSON_ENSURE_ASCII    \u00E9 UPPERCASE, DEL raw
     *   form 2  nlohmann dump() bare           raw UTF-8 bytes
     *   form 3  nlohmann dump(ensure_ascii)    \u00e9 lowercase, \u007f
     *
     * U+00E9 alone separates all three - uppercase escape, raw bytes, lowercase
     * escape - and U+007F separates form 1 from the other two while moving the
     * LENGTH of the payload. Both are asserted in both directions.
     *
     * ⚠️ WHY THE CONTROL CHARACTERS ARE NOT IN THIS PROBE, MEASURED AND NOT
     * ASSUMED: the only emitter of this perimeter that carries client bytes is
     * processConfig(), whose payload comes from the XML files, and the XML
     * WRITER DOES NOT CARRY U+001F OR U+0001 - a probe holding them never
     * reaches io.xml at all (the guard in configGetWireWithProbeOnDisk() caught
     * exactly that, which is why it exists). U+007F and the invalid pair DO get
     * through. The control-character divergence of E4.1a - U+001F differs
     * between the two libraries because its hexadecimal contains a LETTER,
     * U+0001 does not - is therefore asserted on the two carriers that CAN hold
     * them: Tripwire_TheWsApiWireIsAlreadyFormThree just below, and the library
     * tripwire of tests/ParamsJson_test.cpp.
     */
    loadReferenceHouse();

    const std::string probe = std::string("e41s-") + "\xc3\xa9" + "-\x7f-end";
    probeIoName(HOUSE_STRING, probe);

    const std::string wire = configGetWireWithProbeOnDisk(probe);

    //--- form 3: what ships NOW on this emitter, and the point of the ticket.
    EXPECT_TRUE(contains(wire, ASCII_E_LOWER))
            << "the API wire is not form 3";
    EXPECT_TRUE(contains(wire, ASCII_DEL))
            << "the API wire is not form 3 on U+007F";

    //--- form 1: gone. jansson's UPPERCASE hexadecimal and its raw DEL byte
    //are what this emitter used to put on the socket.
    EXPECT_FALSE(contains(wire, ASCII_E_UPPER))
            << "the API wire fell back to FORM 1 (jansson, uppercase hex)";
    EXPECT_FALSE(contains(wire, std::string("\x7f", 1)))
            << "the API wire fell back to FORM 1 on U+007F";

    //--- form 2: raw UTF-8. Never, on any tree. Form 2 is the FAILURE mode of
    //this ticket, not a variant of it: a bare dump() would land here, and a
    //reader who only knows "the tripwire must go red" could take it for
    //success.
    EXPECT_FALSE(contains(wire, RAW_E))
            << "the API wire went to FORM 2 (raw UTF-8) - that is not the "
               "bascule this ticket asks for";
    EXPECT_FALSE(hasAnyByteAbove7f(wire))
            << "the API wire stopped being pure ASCII";
}

TEST_F(JsonApiDispatchWireBytesTest, Tripwire_TheWsApiWireIsAlreadyFormThree)
{
    /* ⭐ AN INVARIANT, AND THE MEASUREMENT BEHIND IT MATTERS.
     *
     * There is NO jansson emitter left on the websocket that carries bytes a
     * client can influence: after E4.1r the only two callers of
     * sendJson(const string &, json_t *, const string &) are the login refusal
     * (a constant, {"success":"false"}) and the two "no data" envelopes. So the
     * websocket crossed to form 3 in E4.1b/l/m/n/o/p/q, not here, and this case
     * is green on BOTH trees ON PURPOSE.
     *
     * It is not decoration: it is the contrast that makes the HTTP tripwire
     * above readable. Without it, "the HTTP wire is form 1" could be read as
     * "the API is form 1", which is false.
     */
    loadReferenceHouse();

    IOBase *io = ListeRoom::Instance().get_io(HOUSE_STRING);
    ASSERT_TRUE(io != nullptr);
    io->set_param("e41s_probe", std::string("e41s-") + "\xc3\xa9" + "-\x1f-\x01-\x7f-end");

    WsTestSession ws;
    ws.send(Json{{ "msg", "get_param" }, { "msg_id", "1" },
                 { "data", {{ "id", HOUSE_STRING }, { "param", "e41s_probe" }} }});
    ASSERT_EQ(1u, ws.count());

    const std::string wire = ws.lastMessage();

    EXPECT_TRUE(contains(wire, ASCII_E_LOWER)) << wire;
    EXPECT_TRUE(contains(wire, ASCII_US_LOW)) << wire;
    EXPECT_TRUE(contains(wire, ASCII_DEL)) << wire;
    EXPECT_TRUE(contains(wire, ASCII_SOH)) << wire;

    EXPECT_FALSE(contains(wire, ASCII_E_UPPER)) << "the WS wire fell back to form 1";
    EXPECT_FALSE(contains(wire, ASCII_US_UP)) << "the WS wire fell back to form 1";
    EXPECT_FALSE(contains(wire, RAW_E)) << "the WS wire fell to form 2";
    EXPECT_FALSE(hasAnyByteAbove7f(wire)) << "the WS wire stopped being pure ASCII";
}

/*******************************************************************************
 * W_ - WITNESSES. Green on both trees. Contracts the migration must not break.
 ******************************************************************************/

TEST_F(JsonApiDispatchWireBytesTest, W_WsEnvelopeWithoutDataOmitsTheDataKeyEntirely)
{
    /* ⛔ THE ONE CONTRACT THE DELETION OF sendJson(json_t *) COULD DESTROY IN
     * SILENCE. That overload OMITTED the "data" member when the pointer was
     * null; the nlohmann overload would write "data":null instead, which is a
     * DIFFERENT DOCUMENT - and golden e40e_ws_get_state_without_data pins it.
     *
     * Asserted on the RAW bytes and not on the parsed document, because
     * "absent" and "null" are the two answers a semantic oracle is worst at
     * telling apart when a case is written carelessly. The whole message is
     * spelled out: the envelope is two pairs and nothing else.
     */
    loadReferenceHouse();

    WsTestSession ws;
    ws.send(Json{{ "msg", "get_state" }, { "msg_id", "1" }});
    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"msg\":\"get_state\",\"msg_id\":\"1\"}", ws.lastMessage());

    ws.clear();
    ws.send(Json{{ "msg", "get_io" }, { "msg_id", "2" }});
    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"msg\":\"get_io\",\"msg_id\":\"2\"}", ws.lastMessage());
}

TEST_F(JsonApiDispatchWireBytesTest, W_WsEnvelopeWithoutDataAndWithoutMsgIdIsOneKey)
{
    //The second half of the same omission: no msg_id either. Two omissions in
    //one envelope, so a rewrite that "always writes msg_id" is caught here and
    //not only by a golden that happens to carry one.
    loadReferenceHouse();

    WsTestSession ws;
    ws.send(Json{{ "msg", "get_state" }});
    ASSERT_EQ(1u, ws.count());
    EXPECT_EQ("{\"msg\":\"get_state\"}", ws.lastMessage());
}

TEST_F(JsonApiDispatchWireBytesTest, W_ConfigGetTopLevelKeysAreSortedOnBothTrees)
{
    //config_files comes before success in BOTH orders - insertion and
    //alphabetical agree here. Kept as a witness precisely BECAUSE it cannot
    //move: it is the control for K_ConfigGetFileNames..., one level down, where
    //they disagree.
    loadReferenceHouse();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));
    ASSERT_EQ(1u, req.count());

    const nlohmann::ordered_json doc = ordered(req.body());
    ASSERT_FALSE(doc.is_discarded());
    EXPECT_EQ("config_files,success", joined(keyOrder(doc)));
}

TEST_F(JsonApiDispatchWireBytesTest, W_ConfigPutWhitelistStillRefusesAnyOtherFilename)
{
    /* AN INVARIANT OF OPERATION (user decision, 2026-08-24): the three accepted
     * filenames are hard coded and the list is NOT to be touched. A migration
     * that read the key differently - or that stopped comparing it at all -
     * would turn processConfig("put") into an arbitrary file write.
     *
     * The refused name is a TRAVERSAL, not merely an unknown word, so the case
     * measures the guard and not a spelling.
     */
    loadReferenceHouse();

    const std::string evil = "../../etc/passwd";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "put" },
                                { "config_files",
                                  {{ evil, "<?xml version=\"1.0\"?><pwned/>" }} }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("false", str(req.bodyJson(), "success"))
            << "the upload whitelist accepted a foreign filename";
}

TEST_F(JsonApiDispatchWireBytesTest, W_ConfigPutAcceptsAWhitelistedNameAndRewritesIt)
{
    //The CONTRAST for the case above: the guard is a filter with two outcomes,
    //not a refusal of everything. A whitelisted name really is written.
    loadReferenceHouse();

    const std::string body = "<?xml version=\"1.0\"?><calaos:rules "
                             "xmlns:calaos=\"http://www.calaos.fr\"/>";

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "put" },
                                { "config_files", {{ "rules.xml", body }} }}));
    ASSERT_EQ(1u, req.count());
    EXPECT_EQ("true", str(req.bodyJson(), "success"));
    EXPECT_EQ(body, rulesXmlOnDisk());
}

TEST_F(JsonApiDispatchWireBytesTest, W_RefusalValuesStayJsonStringsNeverJsonBooleans)
{
    /* Q2 of E4.6, and the contract the whole series is type-strict about: an
     * `int` or a `bool` that became a JSON number or a JSON boolean is a
     * BREAK, not an improvement. The four payloads this ticket moves all carry
     * "true"/"false" as TEXT.
     */
    loadReferenceHouse();

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "get_cover" }, { "id", "nope" }}));
    ASSERT_EQ(1u, req.count());
    const Json cover = req.bodyJson();
    ASSERT_TRUE(member(cover, "success").is_string()) << req.body();
    EXPECT_EQ("false", str(cover, "success"));

    HttpTestRequest req2;
    req2.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));
    ASSERT_EQ(1u, req2.count());
    ASSERT_TRUE(member(req2.bodyJson(), "success").is_string());
    EXPECT_EQ("true", str(req2.bodyJson(), "success"));

    HttpTestRequest req3;
    req3.send(authenticated(Json{{ "action", "config" }, { "type", "reset" }}));
    ASSERT_EQ(1u, req3.count());
    ASSERT_TRUE(member(req3.bodyJson(), "success").is_string());
    EXPECT_EQ("false", str(req3.bodyJson(), "success"));
}

TEST_F(JsonApiDispatchWireBytesTest, W_ContentLengthFollowsTheConfigPayloadByteForByte)
{
    /* The DEL delta changes the LENGTH of the payload, so the one header that
     * has to move with it is Content-Length. Asserted as an EQUALITY with the
     * body actually emitted, never against a constant, so it stays true on both
     * trees and reddens only if the header and the body stop agreeing.
     */
    loadReferenceHouse();

    probeIoName(HOUSE_STRING, std::string("e41s-a\x7f" "b-end", 12));

    HttpTestRequest req;
    req.send(authenticated(Json{{ "action", "config" }, { "type", "get" }}));
    ASSERT_EQ(1u, req.count());

    EXPECT_EQ("application/json", req.header("Content-Type"));
    EXPECT_EQ(std::to_string(req.body().size()), req.header("Content-Length"))
            << "Content-Length stopped following the body";
}

TEST_F(JsonApiDispatchWireBytesTest, W_NoPayloadOfThisPerimeterCarriesAJsonNull)
{
    //"absent key, never null" (E4.0.md:310), checked on the five answers this
    //ticket is the last to move. A `j[k] = nullptr` written by reflex during
    //the port creates the key, and nothing else in the suite looks at these
    //five documents from that angle.
    loadReferenceHouse();

    std::vector<nlohmann::ordered_json> documents;

    documents.push_back(ordered(httpWire(Json{{ "action", "get_mcp_info" }})));
    documents.push_back(ordered(httpWire(Json{{ "action", "get_cover" },
                                              { "id", "nope" }})));
    documents.push_back(ordered(httpWire(Json{{ "action", "get_camera_pic" },
                                              { "id", "nope" }})));
    documents.push_back(ordered(httpWire(Json{{ "action", "config" },
                                              { "type", "get" }})));
    documents.push_back(ordered(httpWire(Json{{ "action", "config" },
                                              { "type", "reset" }})));

    {
        WsTestSession ws;
        ws.send(Json{{ "msg", "get_state" }, { "msg_id", "1" }});
        ASSERT_EQ(1u, ws.count());
        documents.push_back(ordered(ws.lastMessage()));
    }

    for (size_t i = 0; i < documents.size(); i++)
    {
        ASSERT_FALSE(documents[i].is_discarded()) << "document " << i;
        std::vector<const nlohmann::ordered_json *> stack{ &documents[i] };
        while (!stack.empty())
        {
            const nlohmann::ordered_json *j = stack.back();
            stack.pop_back();
            EXPECT_FALSE(j->is_null()) << "a JSON null in document " << i
                                       << ": " << documents[i].dump();
            if (j->is_object() || j->is_array())
                for (auto it = j->begin(); it != j->end(); ++it)
                    stack.push_back(&(*it));
        }
    }
}

TEST_F(JsonApiDispatchWireBytesTest, W_AudioGetCoverRefusalIsOneErrorKeyOnBothTrees)
{
    //The last caller of jansson_from_params() in the two handlers. A single
    //pair, so no order can move; it is here as the witness that the deletion of
    //that call site changes NO byte.
    loadReferenceHouse();

    //"id", not "player_id": getAudioPlayer() reads jsonStringGet(jdata, "id")
    //(JsonApi.cpp), and an id that resolves to nothing is the "unkown"
    //refusal - the EMPTY id is a different message and a different branch.
    const std::string wire = httpWire(Json{{ "action", "audio" },
                                           { "audio_action", "get_cover" },
                                           { "id", "e41s_no_such_player" }});

    EXPECT_EQ("{\"error\":\"unkown player_id\"}", wire);
}
