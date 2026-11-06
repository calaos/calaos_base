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
 * E4.1d - CHARACTERIZATION of the Squeezebox JSON reader.
 *
 * ONE DIRECTION ONLY, and that is the whole point of this ticket. Squeezebox
 * talks to a THIRD PARTY server (Logitech Media Server) over
 * /jsonrpc.js, but it only ever READS the answer:
 * Squeezebox::get_album_cover() builds its request by STRING CONCATENATION,
 * not with a JSON library, and the only json_dumps() of the file goes into a
 * cDebug() trace.
 *
 * ⭐ WHAT THAT MEANS FOR THE BYTE INVARIANTS OF E4.1, SAID PLAINLY SO THAT
 *    NOBODY BELIEVES IN A PROTECTION THIS FILE DOES NOT GIVE:
 *
 *   - There is NO WIRE to protect here. Nothing this file covers is ever sent
 *     to LMS. The escaping decisions of the epic (sorted keys, ensure_ascii,
 *     error_handler_t::replace) apply to exactly ONE dump() in the whole
 *     perimeter, and its output goes to the DEBUG LOG.
 *   - ensure_ascii is therefore LOAD BEARING ONLY ON THE LOG BYTES, and this
 *     file is the only thing in the tree that can see them:
 *     TheTraceStaysPureAsciiEvenOnAccentedMetadata is RED if ensure_ascii is
 *     ever dropped from that dump().
 *   - error_handler_t::replace on that same dump() is DEFENSIVE, NOT LOAD
 *     BEARING, and that is MEASURED, not assumed. The tree fed to the dump can
 *     only come out of the parser, and BOTH libraries refuse invalid UTF-8 at
 *     parse time (jansson: json_loads() answers NULL, "unable to decode byte
 *     0xff"; nlohmann: parse() answers a discarded value). So no production
 *     input can make that dump() throw type_error.316.
 *     Tripwire_InvalidUtf8IsRefusedByTheParserSoTheHandlerIsDefensive pins
 *     that measurement and goes RED the day it stops holding - i.e. the day
 *     the handler becomes load bearing and somebody must be told.
 *     TheTraceReplacesInvalidUtf8InsteadOfThrowing does mutation-detect the
 *     handler, but it feeds the emitter a value THIS FILE built, so it proves
 *     the handler works, NOT that anything can reach it. Both statements are
 *     needed and neither replaces the other.
 *
 *   ⚠️ This is NOT the KNX situation. There, raw bus bytes reached a bare
 *   dump() and a dimmer at 78% killed the driver. Here every byte that reaches
 *   the dump has already been through a JSON parser that validates UTF-8.
 *
 * THE SEAMS are the three free functions of the anonymous namespace below.
 * They carried the jansson body of Squeezebox.cpp VERBATIM in the
 * characterization commit; since the migration commit they FORWARD to the
 * SHIPPED header Audio/SqueezeboxWire.h - the very header Squeezebox.cpp
 * itself includes - so that a mutation of the PRODUCTION reader turns this
 * suite red. ELEVEN assertions and declarations carry a MOVED BY E4.1d mark in
 * this file - count them with grep, the number in this sentence is the one
 * thing here nothing checks.
 *
 * ⚠️ WHAT THIS FILE STILL DOES NOT COVER, measured and consigned rather than
 * hidden: the CALL SITES inside Squeezebox::get_album_cover_json_cb() - the
 * switch on the lookup outcome, the fallback to get_album_cover_std(), the
 * order of the two arguments handed to buildCoverUrl(). Permuting two
 * positional arguments at a call site stayed green in three tickets of this
 * series. The closure for that is TYPING, not testing: buildCoverUrl() takes
 * its host through a named struct (SqueezeboxWire::LmsHost) precisely so that
 * the permutation does not compile.
 *
 * A DELIBERATELY RICH FIXTURE. The recurring defect of the E4.0/E4.1 series is
 * the "poor fixture": a dataset too uniform for a swap of two interchangeable
 * fields to show. Every string below carries its own prefix, no one of them is
 * a substring of another, and the nesting decoys of
 * TheLookupNeverReadsArtworkUrlFromTheWrongNestingLevel put THREE DIFFERENT
 * artwork_url values at three different depths.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <string>
#include <cstdio>

#include "Utils.h"
#include "Params.h"

/* THE PRODUCTION HEADER. Not a copy of it: the very text Squeezebox.cpp
 * includes and the server ships. */
#include "SqueezeboxWire.h"

using std::string;

namespace
{

/*---------------------------------------------------------------------------
 * The outcome of the artwork lookup.
 *
 * Squeezebox.cpp answers FOUR different things today, and it logs three of
 * them differently, so they are three different observable outcomes and not
 * one "failed":
 *   - Found          : result.remoteMeta.artwork_url is a JSON string;
 *   - NoArtworkUrl   : remoteMeta is an object but artwork_url is absent or
 *                      not a string -> cDebugDom "artwork_url not found in
 *                      remoteMeta!";
 *   - NoRemoteMeta   : result is an object but remoteMeta is not
 *                      -> cDebugDom "remoteMeta not found!";
 *   - NoResultObject : the root is not an object, OR "result" is not an
 *                      object. Those two are INDISTINGUISHABLE today - both
 *                      fall through in SILENCE, with no log at all - so they
 *                      are deliberately one value here and not two.
 * All four end the same way for the user: get_album_cover_std().
 *
 * MOVED BY E4.1d: this local enum becomes an alias of the SHIPPED
 * SqueezeboxWire::ArtworkLookup in the migration commit. The enumerator names
 * do not change, so no assertion below moves with it.
 *-------------------------------------------------------------------------*/
//MOVED BY E4.1d: was a local enum carrying the jansson outcomes, is now an
//alias of the SHIPPED one. The enumerator names did not change, so no
//assertion of this file moved with it.
using ArtworkLookup = SqueezeboxWire::ArtworkLookup;

struct LookupOutcome
{
    bool parsed = false;              //false where json_loads() answered NULL
    ArtworkLookup where = ArtworkLookup::NoResultObject;
    string aurl;
};

/*---------------------------------------------------------------------------
 * THE SEAM - parse + navigate.
 *
 * Verbatim body of Squeezebox::get_album_cover_json_cb() (Squeezebox.cpp:774
 * to :853). MOVED BY E4.1d onto SqueezeboxWire::parseResponse() +
 * SqueezeboxWire::findArtworkUrl().
 *-------------------------------------------------------------------------*/
LookupOutcome lookupArtwork(const string &response)
{
    LookupOutcome out;

    Json json;
    if (!SqueezeboxWire::parseResponse(response, json))
        return out; //parsed stays false: this is where the driver logs and falls back

    out.parsed = true;
    out.where = SqueezeboxWire::findArtworkUrl(json, out.aurl);

    return out;
}

/*---------------------------------------------------------------------------
 * THE SEAM - the cDebug() trace.
 *
 * Verbatim body of Squeezebox.cpp:786-791. Answers the empty string exactly
 * where the driver logs nothing: json_loads() answered NULL, or json_dumps()
 * answered NULL. MOVED BY E4.1d onto SqueezeboxWire::parseResponse() +
 * SqueezeboxWire::prettyPrint().
 *-------------------------------------------------------------------------*/
string traceOf(const string &response)
{
    Json json;
    if (!SqueezeboxWire::parseResponse(response, json))
        return string();

    return SqueezeboxWire::prettyPrint(json);
}

/*---------------------------------------------------------------------------
 * THE SEAM - the cover URL.
 *
 * Verbatim body of Squeezebox.cpp:817-841: an artwork_url that already starts
 * with "http" is taken as is, anything else is hung under the LMS host on port
 * 9000. MOVED BY E4.1d onto SqueezeboxWire::buildCoverUrl(), whose second
 * parameter is a NAMED STRUCT so that the two strings cannot be permuted at
 * the call site.
 *-------------------------------------------------------------------------*/
string buildCoverUrl(const string &aurl, const string &host)
{
    return SqueezeboxWire::buildCoverUrl(aurl, SqueezeboxWire::LmsHost{host});
}

/*---------------------------------------------------------------------------
 * Fixture and helpers
 *-------------------------------------------------------------------------*/

//Four disjoint vocabularies. No one of them is a substring of another, so a
//lookup that read the wrong nesting level, or a builder that swapped its two
//arguments, changes an assertion.
const char *const FX_HOST     = "h-lms-livingroom.lan";
const char *const FX_ART_REL  = "a-music/1a2b3c/cover.jpg";
const char *const FX_ART_ABS  = "http://b-artservice.example/cover/9f8e.png";

//A real slim.request "status" answer, trimmed to the shape the driver walks.
//"id" and "params" are there because LMS really sends them and because their
//presence is what makes the SORTED key order of the migration visible.
const char *const LMS_STATUS_ANSWER =
        "{"
        "\"id\":1,"
        "\"method\":\"slim.request\","
        "\"result\":{"
            "\"player_name\":\"p-Salon\","
            "\"remoteMeta\":{"
                "\"title\":\"t-Kind of Blue\","
                "\"artist\":\"r-Miles Davis\","
                "\"artwork_url\":\"a-music/1a2b3c/cover.jpg\""
            "}"
        "}"
        "}";

bool isPureAscii(const string &s)
{
    for (unsigned char c: s)
    {
        if (c >= 0x80)
            return false;
    }
    return true;
}

//Renders a byte string readable in a gtest failure message: a raw \xc3\xa9
//pasted into an assertion log is unreadable and, worse, invisible.
string escaped(const string &s)
{
    string out;
    char buf[8];
    for (unsigned char c: s)
    {
        if (c >= 0x20 && c < 0x7f)
        {
            out += static_cast<char>(c);
        }
        else
        {
            snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

} //namespace

/*******************************************************************************
 * READING - structure and values
 ******************************************************************************/

//The nominal answer: the driver reaches result.remoteMeta.artwork_url and
//nothing else.
TEST(SqueezeboxWire, FindsArtworkUrlInAWellFormedStatusAnswer)
{
    const LookupOutcome out = lookupArtwork(LMS_STATUS_ANSWER);

    ASSERT_TRUE(out.parsed);
    EXPECT_EQ(ArtworkLookup::Found, out.where);
    EXPECT_EQ(FX_ART_REL, out.aurl);
}

//THE ANTI-POOR-FIXTURE CASE. Three DIFFERENT artwork_url values sit at three
//different depths: at the root, inside "result", and inside
//"result"."remoteMeta". Only the deepest one is the right answer, so a reader
//that lost one level of nesting - or gained one - shows here and only here.
TEST(SqueezeboxWire, TheLookupNeverReadsArtworkUrlFromTheWrongNestingLevel)
{
    const char *const decoyed =
            "{"
            "\"artwork_url\":\"z-ROOT-decoy.jpg\","
            "\"result\":{"
                "\"artwork_url\":\"y-RESULT-decoy.jpg\","
                "\"remoteMeta\":{"
                    "\"artwork_url\":\"x-REMOTEMETA-the-real-one.jpg\""
                "}"
            "}"
            "}";

    const LookupOutcome out = lookupArtwork(decoyed);

    ASSERT_TRUE(out.parsed);
    ASSERT_EQ(ArtworkLookup::Found, out.where);
    EXPECT_EQ("x-REMOTEMETA-the-real-one.jpg", out.aurl);
    EXPECT_NE("y-RESULT-decoy.jpg", out.aurl);
    EXPECT_NE("z-ROOT-decoy.jpg", out.aurl);
}

//The three "not found" outcomes are THREE, not one. Each of them logs a
//different line today (or, for NoResultObject, no line at all), so collapsing
//them would be a silent loss of diagnosis.
TEST(SqueezeboxWire, TheThreeMissingLevelsAreThreeDistinctOutcomes)
{
    //remoteMeta present, artwork_url absent
    LookupOutcome a = lookupArtwork("{\"result\":{\"remoteMeta\":{\"title\":\"t-x\"}}}");
    ASSERT_TRUE(a.parsed);
    EXPECT_EQ(ArtworkLookup::NoArtworkUrl, a.where);
    EXPECT_EQ("", a.aurl);

    //result present, remoteMeta absent (a LOCAL file has no remoteMeta at all)
    LookupOutcome b = lookupArtwork("{\"result\":{\"player_name\":\"p-Salon\"}}");
    ASSERT_TRUE(b.parsed);
    EXPECT_EQ(ArtworkLookup::NoRemoteMeta, b.where);

    //remoteMeta present but NOT an object
    LookupOutcome c = lookupArtwork("{\"result\":{\"remoteMeta\":\"m-not-an-object\"}}");
    ASSERT_TRUE(c.parsed);
    EXPECT_EQ(ArtworkLookup::NoRemoteMeta, c.where);

    //no "result" at all: silent fall-through, no log
    LookupOutcome d = lookupArtwork("{\"id\":1,\"method\":\"slim.request\"}");
    ASSERT_TRUE(d.parsed);
    EXPECT_EQ(ArtworkLookup::NoResultObject, d.where);

    //"result" present but not an object: same silent fall-through
    LookupOutcome e = lookupArtwork("{\"result\":[1,2]}");
    ASSERT_TRUE(e.parsed);
    EXPECT_EQ(ArtworkLookup::NoResultObject, e.where);
}

//artwork_url of the WRONG TYPE is not accepted, and above all it is not
//stringified. json_is_string() is the guard, and it must stay one: an
//artwork_url that came back as a number would otherwise become the string "3"
//and be concatenated into an HTTP URL.
TEST(SqueezeboxWire, ArtworkUrlOfTheWrongTypeIsRefused)
{
    const char *const shapes[] = {
        "{\"result\":{\"remoteMeta\":{\"artwork_url\":42}}}",
        "{\"result\":{\"remoteMeta\":{\"artwork_url\":null}}}",
        "{\"result\":{\"remoteMeta\":{\"artwork_url\":true}}}",
        "{\"result\":{\"remoteMeta\":{\"artwork_url\":[\"a-x.jpg\"]}}}",
        "{\"result\":{\"remoteMeta\":{\"artwork_url\":{\"u\":\"a-x.jpg\"}}}}",
    };

    for (const char *s: shapes)
    {
        LookupOutcome out = lookupArtwork(s);
        ASSERT_TRUE(out.parsed) << s;
        EXPECT_EQ(ArtworkLookup::NoArtworkUrl, out.where) << s;
        EXPECT_EQ("", out.aurl) << s;
    }
}

//MALFORMED answers are refused, and the driver falls back to the CLI path.
//Never a half-read answer.
TEST(SqueezeboxWire, MalformedAnswersAreRefused)
{
    EXPECT_FALSE(lookupArtwork("").parsed);
    EXPECT_FALSE(lookupArtwork("{oops").parsed);
    EXPECT_FALSE(lookupArtwork("{\"result\":{}} trailing").parsed);
    EXPECT_FALSE(lookupArtwork("{\"result\":{}").parsed);
    EXPECT_FALSE(lookupArtwork("<html>404 not found</html>").parsed);
}

//THE ACCEPTANCE SET, measured on jansson with no decode flag: a top level
//SCALAR is REFUSED outright (JSON_DECODE_ANY is off), a top level ARRAY is
//accepted and falls through silently. This is the exact contract the
//migration has to keep: nlohmann's parse() accepts a bare "3" and a bare
//"null" where jansson refused them, and without an explicit guard an LMS that
//answered `null` would go from "malformed" to "protocol ok, nothing found".
TEST(SqueezeboxWire, TopLevelScalarsAreRefusedAndArraysFallThroughSilently)
{
    EXPECT_FALSE(lookupArtwork("3").parsed);
    EXPECT_FALSE(lookupArtwork("\"ok\"").parsed);
    EXPECT_FALSE(lookupArtwork("true").parsed);
    EXPECT_FALSE(lookupArtwork("null").parsed);

    LookupOutcome arr = lookupArtwork("[1,2]");
    EXPECT_TRUE(arr.parsed);
    EXPECT_EQ(ArtworkLookup::NoResultObject, arr.where);
}

//⛔ THE ONE MEASURED ACCEPTANCE DIVERGENCE OF THIS FILE, DECLARED and pinned
//here so that changing it again is a deliberate act.
//
//AN ESCAPED NUL, "\u0000": jansson REFUSED it - "\u0000 is not allowed without
//JSON_ALLOW_NUL" in a value, "NUL byte in object key not supported" in a key -
//so the whole answer was a parse error and the driver fell back to the CLI
//path. nlohmann ACCEPTS it and decodes it to a real 0x00 byte inside the
//std::string. So an LMS answer carrying an escaped NUL moves from "malformed,
//fall back" to "parsed, walk it" - and, if it sits in artwork_url, that NUL
//ends up in an HTTP URL.
//
//⚠️ A RAW NUL byte is refused by BOTH libraries (jansson: "control character
//0x0"; nlohmann: discarded), so nothing here rests on the c_str() the old code
//used - measured both ways.
//
//This is not reachable from a normal LMS, whose metadata is text. It is
//reachable from a hostile or broken stream announcement, which is exactly the
//kind of byte this perimeter reads.
TEST(SqueezeboxWire, TheDeclaredAcceptanceDivergence_EscapedNulIsNowAccepted)
{
    //an escaped NUL in the artwork_url itself: parsed now, refused before
    LookupOutcome inValue = lookupArtwork(
            "{\"result\":{\"remoteMeta\":{\"artwork_url\":\"a-x\\u0000y.jpg\"}}}");
    ASSERT_TRUE(inValue.parsed)
            << "the escaped-NUL divergence changed: re-read the comment above";
    EXPECT_EQ(ArtworkLookup::Found, inValue.where);
    EXPECT_EQ(9u, inValue.aurl.size()) << "the NUL was dropped from the value";
    EXPECT_NE(string::npos, inValue.aurl.find('\0'));

    //and in a key
    LookupOutcome inKey = lookupArtwork("{\"a\\u0000b\":1,\"result\":{}}");
    EXPECT_TRUE(inKey.parsed);

    //...but a RAW NUL byte is still refused, by both libraries.
    string rawNul = string("{\"result\":{\"remoteMeta\":{\"artwork_url\":\"a-x");
    rawNul.push_back('\0');
    rawNul += "y.jpg\"}}}";
    //a probe that cannot silently become valid
    ASSERT_NE(string::npos, rawNul.find('\0')) << "the fixture lost its embedded NUL";
    EXPECT_FALSE(lookupArtwork(rawNul).parsed)
            << "a RAW NUL byte became acceptable: that one was refused by BOTH "
               "libraries and the guard contract just changed";
}

/*******************************************************************************
 * THE COVER URL
 ******************************************************************************/

//The two branches, with values that cannot be confused: an absolute URL is
//taken as is, a relative one is hung under the LMS host on port 9000.
TEST(SqueezeboxWire, AbsoluteArtworkUrlsAreTakenAsIsAndRelativeOnesAreHungUnderTheHost)
{
    EXPECT_EQ(FX_ART_ABS, buildCoverUrl(FX_ART_ABS, FX_HOST));

    EXPECT_EQ("http://h-lms-livingroom.lan:9000/a-music/1a2b3c/cover.jpg",
              buildCoverUrl(FX_ART_REL, FX_HOST));

    //the test is "starts with http", not "is a URL": https matches too, and so
    //does anything else starting with those four bytes. That is the shipped
    //contract, pinned so a "fix" is a deliberate act.
    EXPECT_EQ("https://c-secure.example/x.png",
              buildCoverUrl("https://c-secure.example/x.png", FX_HOST));
    EXPECT_EQ("httpFOO", buildCoverUrl("httpFOO", FX_HOST));
}

//Strings SHORTER than the four bytes compared. std::string::compare(0, 4, ...)
//clamps instead of throwing, so this is well defined - but it is exactly the
//kind of edge an "obvious" rewrite with substr(0,4) would break.
TEST(SqueezeboxWire, ShortAndEmptyArtworkUrlsGoThroughTheRelativeBranch)
{
    EXPECT_EQ("http://h-lms-livingroom.lan:9000/", buildCoverUrl("", FX_HOST));
    EXPECT_EQ("http://h-lms-livingroom.lan:9000/ht", buildCoverUrl("ht", FX_HOST));
}

/*******************************************************************************
 * THE TRACE - THE ONLY dump() OF THIS PERIMETER, AND ITS BYTE ORACLES
 *
 * Everything below asserts on the RAW BYTES of the trace. The 145 goldens of
 * the series compare PARSED DOCUMENTS and are blind to escaping by
 * construction, so nothing else in the tree can see any of this.
 ******************************************************************************/

//The shape: four-space indentation, one key per line, ": " between key and
//value. This is JSON_INDENT(4) today and dump(4) after the migration.
TEST(SqueezeboxWire, TheTraceIsIndentedWithFourSpaces)
{
    const string trace = traceOf("{\"result\":{\"remoteMeta\":{\"artwork_url\":\"a-x.jpg\"}}}");

    EXPECT_EQ("{\n"
              "    \"result\": {\n"
              "        \"remoteMeta\": {\n"
              "            \"artwork_url\": \"a-x.jpg\"\n"
              "        }\n"
              "    }\n"
              "}",
              trace)
            << escaped(trace);
}

//A refused answer produces NO trace at all: the driver never reaches its
//json_dumps(). "no trace" and "empty trace" are the same thing here and both
//mean the fallback path was taken.
TEST(SqueezeboxWire, ARefusedAnswerProducesNoTrace)
{
    EXPECT_EQ("", traceOf("{oops"));
    EXPECT_EQ("", traceOf(""));
    EXPECT_EQ("", traceOf("3"));
}

//THE KEY ORDER of the trace.
//IT MOVED EXACTLY ONCE, in the migration commit: jansson emitted in INSERTION
//order, nlohmann::json emits SORTED (user decision of 2026-08-17: sorted keys
//assumed, never ordered_json). This is a DEBUG LOG, read by a human, never
//parsed by anything.
TEST(SqueezeboxWire, TheTraceKeyOrderIsSortedByTheLibrary)
{
    const string trace = traceOf("{\"zulu\":1,\"alpha\":2,\"mike\":3}");

    //MOVED BY E4.1d, and this is the whole of the move: keys used to come out
    //in the INSERTION order of the answer (zulu, alpha, mike), they now come
    //out SORTED. This is a debug log, read by a human, parsed by nothing.
    EXPECT_EQ("{\n"
              "    \"alpha\": 2,\n"
              "    \"mike\": 3,\n"
              "    \"zulu\": 1\n"
              "}",
              trace)
            << escaped(trace);
}

//⭐ RED IF ensure_ascii IS DROPPED (after the migration).
//
//THE NON-ASCII PATH IS REAL, NOT SYNTHETIC, and this is the measurement the
//brief asked for: every string of this document comes from a THIRD PARTY
//server. remoteMeta is the metadata of a REMOTE STREAM - an internet radio
//station name, a track title, an artist name - forwarded by LMS from whatever
//the stream announced. "Café del Mar" and "Björk" are ordinary track titles.
//
//IT MOVED EXACTLY ONCE, in the migration commit: jansson's json_dumps()
//WITHOUT JSON_ENSURE_ASCII (JSON_INDENT(4) and nothing else was all
//Squeezebox.cpp passed) wrote the raw UTF-8 bytes; dump(4, ' ', true, ...)
//writes \u00e9. isPureAscii() below is now what goes red if the flag is ever
//dropped again.
TEST(SqueezeboxWire, TheTraceStaysPureAsciiEvenOnAccentedMetadata)
{
    //é U+00E9, ö U+00F6 and ü U+00FC: three DIFFERENT codepoints in three
    //DIFFERENT keys, so an escape attached to the wrong field shows.
    const string trace = traceOf("{\"result\":{\"remoteMeta\":{"
                                 "\"title\":\"t-Caf\xc3\xa9 del Mar\","
                                 "\"artist\":\"r-Bj\xc3\xb6rk\","
                                 "\"album\":\"b-M\xc3\xbcnchen\"}}}");

    ASSERT_NE("", trace);

    //MOVED BY E4.1d. JSON_INDENT(4) carried no JSON_ENSURE_ASCII, so the trace
    //used to hold the RAW UTF-8 bytes. The dump is now
    //dump(4, ' ', true, replace) and the trace is pure ASCII. THIS IS THE
    //ORACLE: drop ensure_ascii and every assertion below fails.
    EXPECT_TRUE(isPureAscii(trace))
            << "raw UTF-8 bytes reached the trace: " << escaped(trace);

    //no raw byte of any of the three sequences survived...
    EXPECT_EQ(string::npos, trace.find("\xc3\xa9")) << escaped(trace);
    EXPECT_EQ(string::npos, trace.find("\xc3\xb6")) << escaped(trace);
    EXPECT_EQ(string::npos, trace.find("\xc3\xbc")) << escaped(trace);

    //...and the three escapes really are there, one per codepoint, in
    //LOWERCASE hex (jansson wrote \u00E9, nlohmann writes \u00e9; no JSON
    //parser can tell, and no case normalisation is done here on purpose)
    EXPECT_NE(string::npos, trace.find("\\u00e9")) << escaped(trace);
    EXPECT_NE(string::npos, trace.find("\\u00f6")) << escaped(trace);
    EXPECT_NE(string::npos, trace.find("\\u00fc")) << escaped(trace);
    EXPECT_EQ(string::npos, trace.find("\\u00E9")) << escaped(trace);
}

//The hexadecimal CASE of the escapes, pinned with NO case normalisation
//anywhere - normalising it here is exactly the defect that made the original
//tripwire of E4.1a useless. jansson writes \u001F, nlohmann writes \u001f.
//An LMS track title really can carry a control character: it is whatever byte
//the stream announced.
//IT MOVED EXACTLY ONCE, in the migration commit.
TEST(SqueezeboxWire, TheHexCaseOfControlEscapesIsTheMeasuredDelta)
{
    const string trace = traceOf("{\"t\":\"a\\u001fb\"}");

    ASSERT_NE("", trace);

    //MOVED BY E4.1d: UPPERCASE hex -> lowercase hex.
    EXPECT_NE(string::npos, trace.find("\\u001f")) << escaped(trace);
    EXPECT_EQ(string::npos, trace.find("\\u001F")) << escaped(trace);
}

//⭐ THE MEASUREMENT THAT MAKES THE ERROR HANDLER DEFENSIVE.
//
//The tree that reaches the trace dump can ONLY come out of the parser, and
//BOTH libraries refuse invalid UTF-8 at parse time. Measured here on the
//shipped library, in both worlds:
//   - jansson  : json_loads() answers NULL, "unable to decode byte 0xff";
//   - nlohmann : parse(..., nullptr, false) answers a DISCARDED value.
//So no LMS answer, however hostile, can put an invalid byte in front of that
//dump(). error_handler_t::replace on it is a seat belt, not a load-bearing
//guard, and this file says so rather than letting a reader believe otherwise.
//
//THIS CASE GOES RED THE DAY THAT STOPS BEING TRUE - which is the day the
//handler becomes load bearing and somebody has to be told.
TEST(SqueezeboxWire, Tripwire_InvalidUtf8IsRefusedByTheParserSoTheHandlerIsDefensive)
{
    //a lone 0xff inside a track title
    const string hostile = "{\"result\":{\"remoteMeta\":{\"title\":\"t-b\xff""ad\"}}}";

    EXPECT_FALSE(lookupArtwork(hostile).parsed)
            << "the parser started ACCEPTING invalid UTF-8: the error handler of "
               "the trace dump() is now LOAD BEARING, re-read the header of this file";
    EXPECT_EQ("", traceOf(hostile));

    //truncated multi-byte sequence, and a lone UTF-16 surrogate escape: both
    //refused too, by both libraries.
    EXPECT_FALSE(lookupArtwork("{\"t\":\"x\xc3\"}").parsed);
    EXPECT_FALSE(lookupArtwork("{\"t\":\"\\ud800\"}").parsed);
}

//⭐ RED IF error_handler_t::replace IS DROPPED - but read the caveat.
//
//This case feeds the emitter a value THIS FILE built, so it proves the handler
//DOES ITS JOB; it does NOT prove anything can reach it (the tripwire above is
//what says nothing can). Both halves are needed: without this one a dropped
//handler is invisible, without the tripwire above a reader would believe this
//file protects a real path.
//
//IT MOVED EXACTLY ONCE, in the migration commit, and the move IS the whole
//behaviour delta of the two libraries on this path: jansson COULD NOT EVEN
//HOLD an invalid UTF-8 string - json_string() answered NULL and the value was
//never built - whereas nlohmann accepts the bytes into the tree and dump()
//throws type_error.316 unless the handler turns them into U+FFFD.
TEST(SqueezeboxWire, TheTraceReplacesInvalidUtf8InsteadOfThrowing)
{
    //MOVED BY E4.1d. Under jansson this value could not even be BUILT:
    //json_string() answered NULL on an invalid byte and the pair was dropped
    //on the floor. nlohmann accepts the bytes into the tree, and dump() throws
    //type_error.316 out of it unless the handler is there.
    Json hand;
    hand["t"] = string("t-b\xff""ad");

    string trace;
    ASSERT_NO_THROW(trace = SqueezeboxWire::prettyPrint(hand))
            << "the trace dump() threw: error_handler_t::replace is gone";

    //replace -> one \ufffd per bad byte. ignore -> the byte VANISHES and there
    //is no \ufffd. strict -> the ASSERT_NO_THROW above already failed. The
    //three spellings are told apart here, and only here.
    EXPECT_TRUE(isPureAscii(trace)) << escaped(trace);
    EXPECT_NE(string::npos, trace.find("\\ufffd")) << escaped(trace);
    EXPECT_EQ(string::npos, trace.find("\xff")) << escaped(trace);
    EXPECT_NE(string::npos, trace.find("t-b")) << escaped(trace);
    EXPECT_NE(string::npos, trace.find("ad")) << escaped(trace);

    //and a well formed answer is unaffected
    EXPECT_NE("", traceOf("{\"t\":\"t-ok\"}"));
}

/*******************************************************************************
 * TRIPWIRE - the "obvious simplification" that would break the acceptance set
 ******************************************************************************/

//WHY THE MIGRATION MUST KEEP AN EXPLICIT is_object() GUARD.
//
//jansson's json_loads() with flags 0 has no JSON_DECODE_ANY: a top level
//scalar is a PARSE ERROR. nlohmann's parse() has no such notion and accepts
//`null`, `3` and `"ok"` as perfectly good documents. A mechanical port that
//kept only is_discarded() would move an LMS answering `null` from
//"malformed, fall back" to "parsed fine, nothing found" - the same user
//outcome, but a different log and a different acceptance set, and the fiche
//requires the guard contract to stay EXACTLY the one that shipped.
//
//Measured here on the shipped library so the delta is stated at the source.
TEST(SqueezeboxWire, Tripwire_JsonParseAcceptsTopLevelScalarsSoTheGuardIsWhatRefusesThem)
{
    //MOVED BY E4.1d: this used to measure jansson refusing them. It now
    //measures nlohmann ACCEPTING them, which is why parseResponse() carries an
    //explicit is_object()/is_array() guard. Delete that guard and this case
    //still passes - but TopLevelScalarsAreRefusedAndArraysFallThroughSilently
    //goes red, which is the pair of cases that hold the contract together.
    const char *const scalars[] = {"3", "\"ok\"", "true", "false", "null"};
    for (const char *s: scalars)
    {
        const Json j = Json::parse(string(s), nullptr, false);
        EXPECT_FALSE(j.is_discarded())
                << "Json::parse() started REFUSING the top level scalar " << s
                << ": the explicit guard of parseResponse() is now redundant";
        EXPECT_FALSE(j.is_object()) << s;
    }

    //and the two containers parse to what they are
    EXPECT_TRUE(Json::parse(string("{}"), nullptr, false).is_object());
    EXPECT_TRUE(Json::parse(string("[]"), nullptr, false).is_array());
}
