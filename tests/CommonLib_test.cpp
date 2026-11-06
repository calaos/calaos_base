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

// T2.6 — common-lib correctness regressions:
//  - ThreadedQueue::tryPop() must pop (queue drains, no repeated element)
//  - ColorValue::setString() must reject malformed strings without throwing
//  - Calendar month/day roll-over (monthDown Jan→Dec, dayDown commits day)
//  - Params::Parse() safe on high (non-ASCII) bytes

#include <gtest/gtest.h>

#include "ThreadedQueue.h"
#include "ColorUtils.h"
#include "Calendar.h"
#include "Params.h"

//--- ThreadedQueue ----------------------------------------------------------

TEST(ThreadedQueueTest, TryPopDrainsQueue)
{
    ThreadedQueue<int> q;

    int a = 1, b = 2, c = 3;
    q.push(a);
    q.push(b);
    q.push(c);

    int out = 0;
    ASSERT_TRUE(q.tryPop(out));
    EXPECT_EQ(1, out);
    ASSERT_TRUE(q.tryPop(out));
    EXPECT_EQ(2, out); //bug: was 1 forever (front moved but never popped)
    ASSERT_TRUE(q.tryPop(out));
    EXPECT_EQ(3, out);

    EXPECT_FALSE(q.tryPop(out)); //queue fully drained
    EXPECT_TRUE(q.empty());
}

TEST(ThreadedQueueTest, TryPopThenWaitPopSeeDistinctElements)
{
    ThreadedQueue<std::string> q;

    std::string s1 = "first", s2 = "second";
    q.push(s1);
    q.push(s2);

    std::string out;
    ASSERT_TRUE(q.tryPop(out));
    EXPECT_EQ("first", out);
    ASSERT_TRUE(q.waitPop(out));
    EXPECT_EQ("second", out);
    EXPECT_TRUE(q.empty());
}

TEST(ThreadedQueueTest, TryPopOnEmptyOrInvalidQueue)
{
    ThreadedQueue<int> q;
    int out = 0;
    EXPECT_FALSE(q.tryPop(out));

    int v = 42;
    q.push(v);
    q.invalidate();
    EXPECT_FALSE(q.tryPop(out)); //invalidated queue never delivers
}

//--- ColorValue -------------------------------------------------------------

TEST(ColorValueStringTest, MalformedStringsAreRejectedWithoutThrowing)
{
    const char *malformed[] = {
        "rgb(1,2,3",          //missing ')' — used to throw std::out_of_range
        "rgba(0,255,0,0.3",
        "hsl(120,100%,50%",
        "hsla(120,100%,50%,0.3",
        "hsv(120,100%,50%",
        "hsva(120,100%,50%,0.3",
        "rgb(",
        "hsv(",
    };

    for (const char *m: malformed)
    {
        ColorValue c;
        ASSERT_NO_THROW(c.setString(m)) << "input: " << m;
        EXPECT_FALSE(c.isValid()) << "input: " << m;
    }
}

TEST(ColorValueStringTest, WellFormedStringsStillParse)
{
    ColorValue rgb("rgb(10, 20, 30)");
    ASSERT_TRUE(rgb.isValid());
    EXPECT_EQ(10, rgb.getRed());
    EXPECT_EQ(20, rgb.getGreen());
    EXPECT_EQ(30, rgb.getBlue());

    ColorValue rgba("rgba(0, 255, 0, 0.5)");
    ASSERT_TRUE(rgba.isValid());
    EXPECT_EQ(0, rgba.getRed());
    EXPECT_EQ(255, rgba.getGreen());
    EXPECT_EQ(0, rgba.getBlue());

    ColorValue hsl("hsl(120, 100%, 50%)");
    EXPECT_TRUE(hsl.isValid());

    ColorValue hsv("hsv(120, 100%, 50%)");
    EXPECT_TRUE(hsv.isValid());

    ColorValue hex("#FFAA50");
    ASSERT_TRUE(hex.isValid());
    EXPECT_EQ(0xFF, hex.getRed());
    EXPECT_EQ(0xAA, hex.getGreen());
    EXPECT_EQ(0x50, hex.getBlue());
}

//--- Calendar ---------------------------------------------------------------

TEST(CalendarTest, MonthDownFromJanuaryGivesDecember)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setDay(15);
    cal.setMonth(1);

    cal.monthDown();
    EXPECT_EQ(12, cal.month); //bug: was 11 (November), skipping December
}

TEST(CalendarTest, MonthUpFromDecemberGivesJanuary)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setDay(15);
    cal.setMonth(12);

    cal.monthUp();
    EXPECT_EQ(1, cal.month);
}

TEST(CalendarTest, DayDownCommitsDay)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setMonth(3);
    cal.setDay(10);

    cal.dayDown();
    EXPECT_EQ(9, cal.day);

    //wrap-around: from day 1 of March back to 28 of... no, stays in March
    cal.setDay(1);
    cal.dayDown();
    EXPECT_EQ(31, cal.day); //March has 31 days
}

TEST(CalendarTest, GetDayFromDateStaysInBounds)
{
    Calendar cal;

    //A week of consecutive dates must yield 7 in-bounds, consecutive day ids
    int prevId = -1;
    for (int d = 1; d <= 8; d++)
    {
        cal.setYear(2025);
        cal.setMonth(6);
        cal.setDay(d);

        int id = cal.getDayIdFromDate();
        ASSERT_GE(id, 0);
        ASSERT_LE(id, 6); //days[] has 7 entries, index 0..6

        const string dayName = cal.getDayFromDate();
        EXPECT_FALSE(dayName.empty());

        if (prevId >= 0)
            EXPECT_EQ((prevId + 1) % 7, id);
        prevId = id;
    }
}

//--- Params -----------------------------------------------------------------

TEST(ParamsTest, ParseHandlesHighBytes)
{
    //UTF-8 multi-byte chars have bytes >= 0x80: as (signed) char these are
    //negative and passing them straight to isspace() is UB.
    Params p;
    p.Parse("caf\xC3\xA9 \xC3\xA9t\xC3\xA9 fin");

    EXPECT_EQ(3, p.size());
    EXPECT_EQ("caf\xC3\xA9", p["0"]);
    EXPECT_EQ("\xC3\xA9t\xC3\xA9", p["1"]);
    EXPECT_EQ("fin", p["2"]);
}

TEST(ParamsTest, ParseSplitsOnWhitespaceRuns)
{
    Params p;
    p.Parse("one  two\tthree");

    EXPECT_EQ(3, p.size());
    EXPECT_EQ("one", p["0"]);
    EXPECT_EQ("two", p["1"]);
    EXPECT_EQ("three", p["2"]);
}
