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
 * T3.25 (A) - Utils::from_string() / Utils::is_of_type(), the FUNCTION.
 *
 * There is no such thing as "from_string() writes 0 when it fails". There are
 * TWO regimes, and only one of them writes anything at all:
 *
 *   input      old return   old dest        what happened
 *   ""         TRUE         NOT WRITTEN     the sentry of operator>> failed
 *   "  \t "    TRUE         NOT WRITTEN     while skipping whitespace, so
 *                                           num_get never ran and eofbit was
 *                                           set - eof() is the return value
 *   "abc"      false        0               num_get ran and failed: C++11
 *                                           says it stores 0
 *   "12abc"    false        12              num_get ran and SUCCEEDED, the
 *                                           stream just did not reach eof
 *   "12"       true         12              the only honest line
 *   ⭐ "-"      TRUE         0               num_get consumed the sign, found
 *                                           no digit, failed - and eof() said
 *                                           success for the 0 it stored
 *   ⭐ "2^31"   TRUE         INT_MAX         OVERFLOW: consumed whole, num_get
 *                                           SATURATES and sets failbit - and
 *                                           eof() said success for the limit
 *
 * ⭐ THREE families of input LIED about their return code, not one - the blank
 * string, the lone sign and the overflow (T3.25 review, measured with
 * g++ -std=c++17). The blank string is the one the ticket was opened for
 * because it is the only one that leaves the destination ALONE: 312 of the 319
 * call sites in src/ ignore the return code, so for them an empty string meant
 * "keep whatever was in that variable", and most of those destinations are
 * uninitialised locals. The other two always wrote something, which is exactly
 * why nobody noticed that the verdict was wrong.
 *
 * THE SENTINEL IS THE POINT OF THIS FILE. Every destination is seeded with
 * 21845 (0x5555), never with 0. Seeding 0 would make "not written", "written
 * zero" and "zero initialised" the same observation, and every case below
 * would stay green whatever the function did. That fixture mistake is the
 * recidive of this series; it is not repeated here.
 *
 * WHAT IS RED BEFORE THE FIX, AND ON PURPOSE:
 *   - AnEmptyStringIsRefusedAndZeroesTheDestination
 *   - AWhitespaceOnlyStringBehavesExactlyLikeTheEmptyOne
 *   - IsOfTypeRefusesTheEmptyAndTheBlankString
 *   - ABoolThatIsNeitherZeroNorOneIsReportedAsAFailure
 *   - ALoneSignIsNotANumberEitherAlthoughItReachesTheEnd
 *   - AnUnsignedDestinationIsNotSpared (its empty-string half)
 *   - ⭐ AnIntegerOverflowSaturatesAndIsAFAILURE          ]  added by the
 *   - ⭐ ADoubleOverflowSaturatesAndIsAFAILURETOO         ]  T3.25 REVIEW:
 *   - ⭐ IsOfTypeRefusesAnOverflow                        ]  the overflow
 *   - ⭐ FromStringOrKeepKeepsTheDefaultOnAnOverflow      ]  family was not
 *   - ⭐ FromStringOrAlsoRejectsAnOverflow                ]  covered at all
 * Everything else is ALREADY true today and must stay true: those cases are
 * the guard rail that forbids the naive "dest = T{} on failure" correction,
 * which would silently turn "12abc" and "12 " into 0.
 ******************************************************************************/

#include "StringUtils.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>

using std::string;

//0x5555. Not 0, not 1, not a plausible parse result of any input below, and
//distinguishable from every value the function could legitimately write.
static const int    INT_SENTINEL    = 21845;
static const double DOUBLE_SENTINEL = 21845.0;

/*******************************************************************************
 * The defect: an empty (or blank) string
 ******************************************************************************/

TEST(UtilsFromString, AnEmptyStringIsRefusedAndZeroesTheDestination)
{
    //RED BEFORE THE FIX. Today: returns true and leaves dest at 21845.
    //The two assertions are independent and both matter: the return code is
    //what the 7 sites that test it see, the destination is what the other 312
    //see.
    int dest = INT_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string(""), dest))
            << "an empty string is not a number, and must not be reported as one";
    EXPECT_EQ(0, dest)
            << "dest must hold a DEFINED value; 21845 here means it was never "
               "written at all, which is the defect";

    double d = DOUBLE_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string(""), d));
    EXPECT_DOUBLE_EQ(0.0, d);
}

TEST(UtilsFromString, AWhitespaceOnlyStringBehavesExactlyLikeTheEmptyOne)
{
    //RED BEFORE THE FIX. The half of the defect FINDINGS.md never had: the
    //sentry skips whitespace BEFORE deciding, so " " and "\t\n " reach exactly
    //the same dead end as "".
    const char *blanks[] = { " ", "   ", "\t", "  \t ", "\n", " \r\n\t " };

    for (const char *b: blanks)
    {
        int dest = INT_SENTINEL;
        EXPECT_FALSE(Utils::from_string(string(b), dest))
                << "blank input [" << string(b) << "] reported as a number";
        EXPECT_EQ(0, dest)
                << "blank input [" << string(b) << "] left the destination "
                   "untouched";
    }
}

TEST(UtilsFromString, IsOfTypeRefusesTheEmptyAndTheBlankString)
{
    //RED BEFORE THE FIX, and this is why a guard does not save a caller:
    //is_of_type<T>("") answers true today, so the widespread
    //    if (Utils::is_of_type<double>(p)) Utils::from_string(p, x);
    //idiom lets the empty string straight through to the very call that
    //cannot handle it. 10 of the sites carrying a MEANINGFUL default are
    //protected by nothing else than this function telling the truth.
    EXPECT_FALSE(Utils::is_of_type<int>(""));
    EXPECT_FALSE(Utils::is_of_type<int>(" "));
    EXPECT_FALSE(Utils::is_of_type<int>("  \t "));
    EXPECT_FALSE(Utils::is_of_type<double>(""));
    EXPECT_FALSE(Utils::is_of_type<double>("   "));

    //unchanged, and pinned so the fix cannot be "return false always"
    EXPECT_TRUE(Utils::is_of_type<int>("12"));
    EXPECT_TRUE(Utils::is_of_type<double>("1.5"));
    EXPECT_FALSE(Utils::is_of_type<int>("12abc"));
    EXPECT_FALSE(Utils::is_of_type<int>("abc"));
}

TEST(UtilsFromString, ABoolThatIsNeitherZeroNorOneIsReportedAsAFailure)
{
    //RED BEFORE THE FIX, and a real (small) behaviour change worth naming:
    //num_get<bool> reads "12", finds it is neither 0 nor 1, sets failbit AND
    //stores true - while consuming the whole string, so eof() is true and the
    //old return code said "success". libstdc++ leaves the destination at true,
    //which the fix does not change; only the verdict changes.
    bool b = false;
    EXPECT_FALSE(Utils::from_string(string("12"), b));

    //the two values a bool can honestly be read from
    b = true;
    EXPECT_TRUE(Utils::from_string(string("0"), b));
    EXPECT_FALSE(b);
    b = false;
    EXPECT_TRUE(Utils::from_string(string("1"), b));
    EXPECT_TRUE(b);
}

/*******************************************************************************
 * ALREADY TRUE TODAY - the guard rail against over-correcting
 ******************************************************************************/

TEST(UtilsFromString, ANonNumericStringIsRefusedAndZeroesTheDestination)
{
    //GREEN BEFORE AND AFTER. This is the OTHER regime: num_get runs, fails,
    //and C++11 makes it store 0. Nothing to fix here, everything to preserve.
    const char *rubbish[] = { "abc", "true", "false", "." };

    for (const char *r: rubbish)
    {
        int dest = INT_SENTINEL;
        EXPECT_FALSE(Utils::from_string(string(r), dest))
                << "input [" << string(r) << "]";
        EXPECT_EQ(0, dest) << "input [" << string(r) << "]";
    }
}

TEST(UtilsFromString, ALoneSignIsNotANumberEitherAlthoughItReachesTheEnd)
{
    //RED BEFORE THE FIX, and a THIRD sub-regime measured on this tree rather
    //than assumed: "-" and "+" are consumed WHOLE by num_get, which then fails
    //and stores 0. eofbit AND failbit are both set, so the old return value -
    //eof() alone - said "success" while the value was garbage-in-disguise.
    //Unlike "" the destination IS written here, so only the verdict changes.
    const char *signs[] = { "-", "+" };

    for (const char *s: signs)
    {
        int dest = INT_SENTINEL;
        EXPECT_FALSE(Utils::from_string(string(s), dest))
                << "input [" << string(s) << "] reported as a number";
        EXPECT_EQ(0, dest) << "input [" << string(s) << "]";
    }
}

TEST(UtilsFromString, APartialPrefixKeepsItsParsedValueAndIsStillReported)
{
    //GREEN BEFORE AND AFTER, and it is the assertion that FORBIDS the naive
    //correction "if it did not fully succeed, write T{}". "12abc" DID parse a
    //12; throwing it away would change 46 initialised call sites and every
    //config value that carries a unit or a stray character.
    int dest = INT_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("12abc"), dest));
    EXPECT_EQ(12, dest);

    double d = DOUBLE_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("1.5deg"), d));
    EXPECT_DOUBLE_EQ(1.5, d);

    //"1,5" in a C locale is exactly the same story: 1 is read, ",5" is not
    d = DOUBLE_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("1,5"), d));
    EXPECT_DOUBLE_EQ(1.0, d);
}

TEST(UtilsFromString, ATrailingSpaceIsStillRejectedAndKeepsItsValue)
{
    //GREEN BEFORE AND AFTER. Same guard rail, second orthography: a config
    //value written "12 " must keep reading as 12, not become 0.
    int dest = INT_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("12 "), dest));
    EXPECT_EQ(12, dest);
}

TEST(UtilsFromString, AWellFormedNumberIsAcceptedAndWritten)
{
    //GREEN BEFORE AND AFTER, the control: a fix that refused everything would
    //fail here.
    int dest = INT_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("12"), dest));
    EXPECT_EQ(12, dest);

    dest = INT_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("-5"), dest));
    EXPECT_EQ(-5, dest);

    dest = INT_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("0"), dest));
    EXPECT_EQ(0, dest) << "a real zero must still be readable as a zero";

    //LEADING whitespace is skipped by the sentry and has always been accepted
    dest = INT_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("  12"), dest));
    EXPECT_EQ(12, dest);

    double d = DOUBLE_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("1.5"), d));
    EXPECT_DOUBLE_EQ(1.5, d);

    //the C locale is imbued explicitly, so a decimal point is a decimal point
    //whatever LC_NUMERIC says
    d = DOUBLE_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("-0.125"), d));
    EXPECT_DOUBLE_EQ(-0.125, d);
}

TEST(UtilsFromString, AnUnsignedDestinationIsNotSpared)
{
    //The OLA defect used an unsigned channel number. Same two regimes, same
    //sentinel, and the type of the destination changes nothing.
    unsigned short dest = static_cast<unsigned short>(INT_SENTINEL);
    EXPECT_FALSE(Utils::from_string(string(""), dest));
    EXPECT_EQ(0u, dest);

    dest = static_cast<unsigned short>(INT_SENTINEL);
    EXPECT_TRUE(Utils::from_string(string("512"), dest));
    EXPECT_EQ(512u, dest);
}

/*******************************************************************************
 * The two helpers T3.25 adds
 ******************************************************************************/

TEST(UtilsFromString, FromStringOrKeepLeavesTheDefaultAloneOnABlankString)
{
    //NEW API, lands with the fix. This is what the 45 call sites carrying an
    //initialised destination were relying on from_string() to do BY ACCIDENT,
    //said out loud: nothing readable at all -> the default stands.
    int port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string(""), port));
    EXPECT_EQ(1883, port) << "an absent value must not cost the default";

    EXPECT_FALSE(Utils::from_string_or_keep(string("   "), port));
    EXPECT_EQ(1883, port);

    //but anything READABLE still lands, partial reads included - that is the
    //whole difference with from_string_or() below
    EXPECT_TRUE(Utils::from_string_or_keep(string("1234"), port));
    EXPECT_EQ(1234, port);

    port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("12abc"), port));
    EXPECT_EQ(12, port) << "a partial read is kept, exactly as before T3.25";

    /* ⭐ CHANGED BY THE T3.25 REVIEW, and it is the point of the helper.
     * The first cut guarded the BLANK STRING ONLY and then delegated, so
     * "abc" wrote 0 over the default. from_string_or_keep() now writes `dest`
     * if and only if num_get produced a value FROM THE INPUT - which "abc"
     * does not. The default is what the caller asked to protect; protecting it
     * against a blank string but not against rubbish was an accident of the
     * implementation, not a contract anybody wrote down.
     *
     * ⛔ The eventlog per_page site depends on the OPPOSITE ("abc" -> 0 ->
     * refused, T3.19) and is the reason from_string_unless_blank() exists.
     */
    port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("abc"), port));
    EXPECT_EQ(1883, port) << "nothing was read from the input, so the default "
                             "stands - see from_string_unless_blank() for the "
                             "one call site that needs the other answer";

    port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("-"), port));
    EXPECT_EQ(1883, port) << "a lone sign is not a value either";
}

TEST(UtilsFromString, FromStringOrReturnsTheDefaultOnEveryFailure)
{
    //NEW API, lands with the fix. The stricter of the two: a partial read is
    //a failure like any other. This is why it is NOT retrofitted onto the
    //existing call sites.
    EXPECT_EQ(100, Utils::from_string_or(string(""), 100));
    EXPECT_EQ(100, Utils::from_string_or(string("  "), 100));
    EXPECT_EQ(100, Utils::from_string_or(string("abc"), 100));
    EXPECT_EQ(100, Utils::from_string_or(string("12abc"), 100))
            << "the strict form discards a partial read, unlike "
               "from_string_or_keep()";
    EXPECT_EQ(42, Utils::from_string_or(string("42"), 100));
    EXPECT_DOUBLE_EQ(1.5, Utils::from_string_or(string("1.5"), 9.0));
}

/*******************************************************************************
 * ⭐ OVERFLOW - the THIRD family whose verdict T3.25 changed, and the one this
 * file did not cover at all before the review.
 *
 * The T3.25 review measured what the header claimed ("WHAT CHANGED, AND IT IS
 * EXACTLY ONE INPUT: the blank string") and found it false: the return value
 * flips from true to false on THREE families - the blank string, the lone
 * sign, and OVERFLOW. Only the first leaves the destination untouched, which
 * is why only the first was noticed.
 *
 * ⚠️ WHY THIS BLOCK IS NECESSARY, stated precisely. The saturated VALUE was
 * already pinned elsewhere in the tree - core/JsonApiSession_test.cpp
 * (EventLogSaturatesAVeryLargePerPage) and core/JsonApiInputGuards_test.cpp
 * (AHugePerPageSaturatesToIntMaxAndIsHarmless) both assert 2147483647 - and
 * that value is IDENTICAL before and after T3.25. The discovery of the ticket
 * on this family is therefore NOT the INT_MAX, it is the `false`. Before this
 * block, a partial revert of T3.25 restricted to overflow - one that keeps the
 * blank string refused and only puts the saturated case back on iss.eof()
 * alone - made NOTHING in the tree go red. Measured, not assumed.
 ******************************************************************************/

TEST(UtilsFromString, AnIntegerOverflowSaturatesAndIsAFAILURE)
{
    /* The VALUE does not move: num_get consumes every character, clamps to the
     * limit of the type and sets failbit. The RETURN does move - the old
     * iss.eof() said "success" because the whole string WAS consumed.
     */
    const int intMax = std::numeric_limits<int>::max();
    const int intMin = std::numeric_limits<int>::min();

    const char *tooBig[] = { "2147483648", "4294967296", "99999999999999999999" };
    for (const char *r: tooBig)
    {
        int dest = INT_SENTINEL;
        EXPECT_FALSE(Utils::from_string(string(r), dest))
                << "input [" << string(r) << "] is a parse FAILURE, however "
                   "completely it was consumed";
        EXPECT_EQ(intMax, dest)
                << "input [" << string(r) << "]: the saturated value is still "
                   "published - T3.25 changes the verdict, never this value";
    }

    int dest = INT_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("-2147483649"), dest));
    EXPECT_EQ(intMin, dest);

    //the boundary that must stay a success
    dest = INT_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("2147483647"), dest));
    EXPECT_EQ(intMax, dest);
}

TEST(UtilsFromString, ADoubleOverflowSaturatesAndIsAFAILURETOO)
{
    //Same story one type over: HUGE_VAL is stored and failbit is set.
    double dest = DOUBLE_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("1e400"), dest));
    EXPECT_DOUBLE_EQ(std::numeric_limits<double>::max(), dest);

    dest = DOUBLE_SENTINEL;
    EXPECT_FALSE(Utils::from_string(string("-1e400"), dest));
    EXPECT_DOUBLE_EQ(-std::numeric_limits<double>::max(), dest);

    /* ⚠️ NOT an overflow, and worth pinning next to the ones that are: an
     * integer literal far past INT_MAX is a perfectly ordinary double.
     */
    dest = DOUBLE_SENTINEL;
    EXPECT_TRUE(Utils::from_string(string("99999999999999999999"), dest));
    EXPECT_DOUBLE_EQ(1e20, dest);
}

TEST(UtilsFromString, IsOfTypeRefusesAnOverflow)
{
    /* The guard and the parse it guards share Detail::parse(), so they cannot
     * disagree. Ten call sites in src/ are protected by is_of_type() ALONE
     * (the step=1.0 family), and this is the input that used to walk past it.
     */
    EXPECT_FALSE(Utils::is_of_type<int>(string("2147483648")));
    EXPECT_FALSE(Utils::is_of_type<int>(string("99999999999999999999")));
    EXPECT_FALSE(Utils::is_of_type<double>(string("1e400")));

    EXPECT_TRUE(Utils::is_of_type<int>(string("2147483647")));
    EXPECT_TRUE(Utils::is_of_type<double>(string("99999999999999999999")));
}

TEST(UtilsFromString, FromStringOrKeepKeepsTheDefaultOnAnOverflow)
{
    /* ⭐ THE T3.25 REVIEW'S CORRECTION, and the reason the helper was rewritten
     * rather than re-documented. MEASURED on the first cut of this ticket:
     *
     *     int port = 1883;
     *     from_string_or_keep("99999999999999999999", port);   // -> false
     *     port == 2147483647;                                  // ⛔
     *
     * The function whose entire purpose is "the default survives what cannot
     * be read" destroyed the default on the one failure mode where num_get
     * leaves something behind. Twenty call sites depended on it: the mqtt port
     * and keepalive, RemoteUI brightness, the KNX eis sentinel, the ColorUtils
     * alpha, the two JSON-API ports, the Wago Modbus port, the AVReceiver zone.
     */
    int port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("99999999999999999999"), port));
    EXPECT_EQ(1883, port) << "an overflow is a failure, and a failure must not "
                             "cost the default this helper exists to protect";

    port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("2147483648"), port));
    EXPECT_EQ(1883, port);

    double alpha = 1.0;
    EXPECT_FALSE(Utils::from_string_or_keep(string("1e400"), alpha));
    EXPECT_DOUBLE_EQ(1.0, alpha);

    //and the boundary still lands, because it is not a failure
    port = 1883;
    EXPECT_TRUE(Utils::from_string_or_keep(string("2147483647"), port));
    EXPECT_EQ(std::numeric_limits<int>::max(), port);
}

TEST(UtilsFromString, FromStringUnlessBlankPublishesTheSaturatedOverflow)
{
    /* The OTHER half of the old helper, and the one JsonApi.cpp's eventlog
     * needs: T3.19 documents, and two suites pin, that per_page:"99999999999"
     * is ANSWERED with 2147483647 echoed back. Only a BLANK value keeps the
     * default here.
     */
    int perPage = 100;
    EXPECT_FALSE(Utils::from_string_unless_blank(string(""), perPage));
    EXPECT_EQ(100, perPage);

    perPage = 100;
    EXPECT_FALSE(Utils::from_string_unless_blank(string("  \t "), perPage));
    EXPECT_EQ(100, perPage);

    perPage = 100;
    EXPECT_FALSE(Utils::from_string_unless_blank(string("99999999999"), perPage));
    EXPECT_EQ(std::numeric_limits<int>::max(), perPage)
            << "T3.19 answers a huge per_page instead of refusing it";

    perPage = 100;
    EXPECT_FALSE(Utils::from_string_unless_blank(string("abc"), perPage));
    EXPECT_EQ(0, perPage) << "and this 0 is what the T3.19 range guard refuses";

    perPage = 100;
    EXPECT_FALSE(Utils::from_string_unless_blank(string("1,5"), perPage));
    EXPECT_EQ(1, perPage) << "T3.19's documented partial read";
}

TEST(UtilsFromString, FromStringOrAlsoRejectsAnOverflow)
{
    //The strict form was already right on this family - it tests the return
    //value of from_string() and nothing else - and this pins that it stays so.
    EXPECT_EQ(1883, Utils::from_string_or(string("99999999999999999999"), 1883));
    EXPECT_EQ(1883, Utils::from_string_or(string("2147483648"), 1883));
    EXPECT_DOUBLE_EQ(1.0, Utils::from_string_or(string("1e400"), 1.0));
    EXPECT_EQ(std::numeric_limits<int>::max(),
              Utils::from_string_or(string("2147483647"), 1883));
}
