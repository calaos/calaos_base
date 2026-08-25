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

/*
 * E4.1c -- the three jansson RESIDUES that produce no JSON at all.
 *
 * This ticket deletes three things and nothing else:
 *   1. src/bin/calaos_server/IO/ExternProc.h        #include <jansson.h>
 *   2. src/bin/calaos_server/IO/Web/WebCtrl.cpp     #include <jansson.h>
 *   3. src/bin/calaos_server/HttpClient.cpp         #ifndef json_array_foreach
 *                                                   #define json_array_foreach(...)
 *
 * NOT ONE BYTE OF ANY WIRE MOVES. Measured, not assumed: the three files
 * contain ZERO calls to dump() and ZERO nlohmann emission (the only
 * `json_*` tokens in the whole perimeter are the two include lines and the
 * body of the dead macro). There is therefore no emitter here to hang an
 * ensure_ascii / error_handler oracle on -- see the ticket report for the
 * measurement. What CAN break is the BUILD, and only the build, so that is
 * exactly what this suite pins.
 *
 * TWO CONTRACTS, and they are the whole risk of the ticket:
 *
 * (A) ExternProc.h keeps handing <jansson.h> to everyone downstream.
 *     Line 26 (`#include <jansson.h>`) is redundant with line 27
 *     (`#include "Jansson_Addition.h"` -> src/lib/Jansson_Addition.h:24
 *     `#include <jansson.h>`). Deleting 26 is a no-op for the include graph;
 *     deleting BOTH is what breaks 10 translation units, and that is E4.1x's
 *     job, not this ticket's. ExternProcHeaderAloneStillProvidesTheJanssonApi
 *     is the runtime witness: this translation unit includes <gtest/gtest.h>,
 *     a handful of std headers -- none of which can define JANSSON_VERSION_HEX
 *     -- and "ExternProc.h". If jansson is usable below, ExternProc.h is the
 *     one that brought it.
 *
 * (B) json_array_foreach comes from jansson itself, so HttpClient's #ifndef
 *     compat block can never have fired. jansson gained the macro in 2.5 and
 *     configure.ac:51 pins the floor at `jansson >= 2.5`. The macro sits in a
 *     .cpp, not a header, so it cannot leak to another unit either -- the
 *     sixteen real json_array_foreach call sites of the tree (JsonApi.cpp,
 *     both handlers, Wago, OLA, ScriptExtern) are all in OTHER translation
 *     units and all take it from <jansson.h>.
 *
 * The four TripwireSource_* cases read the shipped sources through
 * CALAOS_TOP_SRCDIR, so a mutation of PRODUCTION -- putting either include
 * back, putting the macro back, or dropping the Jansson_Addition.h
 * delegation -- reddens this suite. Each pins a DIFFERENT deletion, so four
 * independent defects give four different failing cases.
 *
 * OWNED BY E4.1x. This whole file is transitional: it calls the jansson C API
 * on purpose, and the delegation it pins is the very line E4.1x removes. When
 * jansson leaves the build, this suite goes with it -- it is one of the
 * `grep -rn 'json_t\|jansson' src tests` hits E4.1x's acceptance criterion 1
 * will report, and the correct answer there is to delete the file, not to
 * rescue it.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

//The header under test. Deliberately the only calaos include of this unit.
#include "ExternProc.h"

//Did the header above make jansson visible? JANSSON_VERSION_HEX is defined by
//<jansson.h> and by nothing else in this translation unit.
#if defined(JANSSON_VERSION_HEX)
static const bool kExternProcHeaderProvidesJansson = true;
#else
static const bool kExternProcHeaderProvidesJansson = false;
#endif

//Is json_array_foreach the NATIVE jansson macro? This unit never defines it,
//and HttpClient.cpp's compat copy lives in a .cpp so it cannot reach here.
#if defined(json_array_foreach)
static const bool kJanssonProvidesArrayForeach = true;
#else
static const bool kJanssonProvidesArrayForeach = false;
#endif

namespace
{

#ifndef CALAOS_TOP_SRCDIR
#error "CALAOS_TOP_SRCDIR must be passed by the build (see tests/Makefile.am)"
#endif

//Read a shipped source file. Returns false when the file cannot be opened, so
//that a mis-wired path FAILS the case instead of quietly asserting on "".
bool readShippedSource(const std::string &relative, std::string &out)
{
    const std::string path = std::string(CALAOS_TOP_SRCDIR) + "/" + relative;
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int countOccurrences(const std::string &haystack, const std::string &needle)
{
    int n = 0;
    for (std::string::size_type p = haystack.find(needle);
         p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

}

/*
 * (A) The provision contract. Green on master (lines 26 AND 27 both provide
 * jansson), green after E4.1c (line 27 alone provides it), RED the day both
 * providers are gone -- which is precisely the E4.1x boundary.
 *
 * The fixture is deliberately not a single key: two distinct keys with two
 * distinct values, checked back by name, so that a decoder that returned the
 * wrong member, or swapped the two, does not slip through.
 */
TEST(JanssonResidues, ExternProcHeaderAloneStillProvidesTheJanssonApi)
{
    ASSERT_TRUE(kExternProcHeaderProvidesJansson)
        << "IO/ExternProc.h no longer makes <jansson.h> visible. Ten translation "
           "units take jansson through this header and nothing else "
           "(EventManager.cpp, IO/Scenario.cpp, KNXCtrl.cpp, "
           "KNXExternProc_main.cpp, OLACtrl.cpp, OLAExternProc_main.cpp, "
           "WagoMap.cpp, WagoExternProc_main.cpp, ScriptBindings.cpp, "
           "ScriptExtern_main.cpp). If this is E4.1x doing its job, give each "
           "of them its own include; if it is E4.1c, put line 27 back.";

#if defined(JANSSON_VERSION_HEX)
    json_t *root = json_object();
    ASSERT_NE(nullptr, root);
    ASSERT_EQ(0, json_object_set_new(root, "opcode", json_string("first")));
    ASSERT_EQ(0, json_object_set_new(root, "payload", json_string("second")));

    json_t *opcode = json_object_get(root, "opcode");
    json_t *payload = json_object_get(root, "payload");
    ASSERT_NE(nullptr, opcode);
    ASSERT_NE(nullptr, payload);
    EXPECT_STREQ("first", json_string_value(opcode));
    EXPECT_STREQ("second", json_string_value(payload));
    EXPECT_EQ(2u, json_object_size(root));

    char *dumped = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
    ASSERT_NE(nullptr, dumped);
    EXPECT_EQ(std::string("{\"opcode\":\"first\",\"payload\":\"second\"}"),
              std::string(dumped));
    free(dumped);
    json_decref(root);
#endif
}

/*
 * (B) The premise under HttpClient's compat macro: jansson ships
 * json_array_foreach itself, at a version configure.ac already demands.
 *
 * The fixture carries THREE distinct elements in a non-palindromic order, and
 * the case asserts the (index, value) PAIRS, not just the values: reversing
 * the array, or wiring the index to the wrong element, both show up.
 */
TEST(JanssonResidues, JanssonProvidesArrayForeachNativelyAtTheConfiguredFloor)
{
    ASSERT_TRUE(kJanssonProvidesArrayForeach)
        << "jansson stopped defining json_array_foreach. HttpClient.cpp's "
           "#ifndef compat copy was deleted by E4.1c on the premise that this "
           "can never happen (configure.ac:51 pins jansson >= 2.5).";

//REVIEW E4.1c/R3: the guard below must name BOTH symbols. Guarding only on
//JANSSON_VERSION_HEX left the json_array_foreach() call sites of this body
//outside any macro guard, so a jansson that shipped the header WITHOUT the
//macro did not turn this case red -- it failed to COMPILE, and the ASSERT_TRUE
//message above could never be printed. Constructible: the review built it with
//a screening header plus #include_next, without touching the image.
#if defined(JANSSON_VERSION_HEX) && defined(json_array_foreach)
    //2.5 is where jansson added json_array_foreach, and configure.ac:51 asks
    //for exactly that floor. 0x020500 == 2.5.0.
    EXPECT_GE(JANSSON_VERSION_HEX, 0x020500)
        << "built against jansson " << JANSSON_VERSION
        << ", below the >= 2.5 floor of configure.ac:51";

    json_t *arr = json_array();
    ASSERT_NE(nullptr, arr);
    ASSERT_EQ(0, json_array_append_new(arr, json_string("alpha")));
    ASSERT_EQ(0, json_array_append_new(arr, json_string("beta")));
    ASSERT_EQ(0, json_array_append_new(arr, json_string("gamma")));

    std::vector<std::pair<size_t, std::string>> seen;
    size_t idx;
    json_t *value;
    json_array_foreach(arr, idx, value)
        seen.push_back(std::make_pair(idx, std::string(json_string_value(value))));

    ASSERT_EQ(3u, seen.size());
    EXPECT_EQ(0u, seen[0].first);
    EXPECT_EQ(std::string("alpha"), seen[0].second);
    EXPECT_EQ(1u, seen[1].first);
    EXPECT_EQ(std::string("beta"), seen[1].second);
    EXPECT_EQ(2u, seen[2].first);
    EXPECT_EQ(std::string("gamma"), seen[2].second);

    //The empty case, the one the compat copy and the native macro had to agree
    //on: zero iterations, no dereference of a null value.
    json_t *empty = json_array();
    ASSERT_NE(nullptr, empty);
    int rounds = 0;
    json_array_foreach(empty, idx, value)
        rounds++;
    EXPECT_EQ(0, rounds);

    json_decref(empty);
    json_decref(arr);
#endif
}

/*
 * The four source tripwires. Each pins ONE deletion of this ticket, so that
 * re-introducing any of them -- which the ticket forbids by name for the
 * header -- reddens a DIFFERENT case.
 *
 * TripwireSource_ExternProcHeaderStillDelegatesToJanssonAddition is the one
 * that was already true before the deletions; it shipped with the
 * characterization commit and pins the line that makes them a no-op.
 */
TEST(JanssonResidues, TripwireSource_ExternProcHeaderStillDelegatesToJanssonAddition)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/IO/ExternProc.h", src));
    ASSERT_FALSE(src.empty());

    EXPECT_EQ(1, countOccurrences(src, "#include \"Jansson_Addition.h\""))
        << "this is what makes the deletion of `#include <jansson.h>` a no-op. "
           "Remove it and the ten downstream units lose jansson -- that is "
           "E4.1x, and it owes each of them its own include.";
}

TEST(JanssonResidues, TripwireSource_ExternProcHeaderDoesNotIncludeJanssonDirectly)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/IO/ExternProc.h", src))
        << "cannot read the shipped header under CALAOS_TOP_SRCDIR="
        << CALAOS_TOP_SRCDIR;
    ASSERT_FALSE(src.empty());

    EXPECT_EQ(0, countOccurrences(src, "#include <jansson.h>"))
        << "E4.1c removed this include because Jansson_Addition.h already "
           "brings it in on the next line. Do not put it back: the header is "
           "the transitive jansson provider for ten units and E4.1x is the "
           "ticket that unwires them, one own include at a time.";
    EXPECT_EQ(0, countOccurrences(src, "jansson.h"));
}

TEST(JanssonResidues, TripwireSource_WebCtrlMentionsJanssonNowhere)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/IO/Web/WebCtrl.cpp", src));
    ASSERT_FALSE(src.empty());

    EXPECT_EQ(0, countOccurrences(src, "jansson"))
        << "WebCtrl.cpp parses XML with pugixml and emits no JSON at all; its "
           "include was dead when E4.1c removed it.";
    EXPECT_EQ(0, countOccurrences(src, "json_"));
}

TEST(JanssonResidues, TripwireSource_HttpClientDoesNotRedefineArrayForeach)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/HttpClient.cpp", src));
    ASSERT_FALSE(src.empty());

    EXPECT_EQ(0, countOccurrences(src, "json_array_foreach"))
        << "the pre-2.5 compat macro was deleted by E4.1c. jansson defines it "
           "natively at the >= 2.5 floor of configure.ac:51, and "
           "JanssonProvidesArrayForeachNativelyAtTheConfiguredFloor proves it "
           "on the jansson this build actually links against.";
    EXPECT_EQ(0, countOccurrences(src, "jansson"));
}
