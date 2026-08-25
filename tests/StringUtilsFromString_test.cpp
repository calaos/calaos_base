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
 *
 * The first two lines are the whole ticket: the ONLY inputs that LIE about
 * their return code are also the ONLY ones that leave the destination alone.
 * 312 of the 319 call sites in src/ ignore that return code, so for them an
 * empty string means "keep whatever was in that variable" - and 173 of those
 * destinations are uninitialised locals.
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

    port = 1883;
    EXPECT_FALSE(Utils::from_string_or_keep(string("abc"), port));
    EXPECT_EQ(0, port) << "unreadable-but-not-blank has always written 0 here";
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
