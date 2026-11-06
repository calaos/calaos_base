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

/******************************************************************************
 * E4.3a — first tests ever for TimeRange (src/lib/TimeRange.h) and for the
 * evaluation of InPlageHoraire (src/bin/calaos_server/IO/InPlageHoraire.cpp).
 * Both files had zero test reference before this suite.
 *
 * What is pinned here
 * -------------------
 *  - TimeRange: the three constructors (default, `proto` string, Params), the
 *    seconds-of-day conversion of an HTYPE_NORMAL bound, the +1/-1 clamping of
 *    the offsets, equality, the proto/Params serialization round trip and the
 *    day-of-week bitset.
 *  - InPlageHoraire: the *observable* result of hasChanged() - inclusive
 *    bounds, empty schedule, inverted (end < start) range wrapping over
 *    midnight into the next day, weekday selection, month mask, disabled IO,
 *    union of several ranges.
 *  - Calendar: only what tests/CommonLib_test.cpp does NOT already cover
 *    (it owns monthUp/monthDown/dayDown/getDayIdFromDate). Here: the leap-year
 *    rule, the day clamping done by setMonth()/setYear(), the dayUp() wrap and
 *    the hour/minute/second wrap + zero padding.
 *
 * Deliberately NOT tested against implementation details
 * ------------------------------------------------------
 * InPlageHoraire::LoadFromXml()/SaveToXml() are about to be ported to pugixml
 * (E4.4d). Nothing below looks at XML: the schedules are built through the
 * public AddMonday()/... API and the verdict is read through get_value_bool().
 * The XML persistence of an InPlageHoraire is covered - also without touching
 * the markup - by tests/core/ConfigRoundTrip_test.cpp (E4.3b).
 *
 * About the clock
 * ---------------
 * InPlageHoraire::hasChanged() reads the wall clock itself (time(NULL) +
 * localtime()) and there is no seam to inject a fake one. The tests that need
 * an exact position relative to "now" therefore run inside a *stable window*:
 * the second-of-day is read before and after the call and the attempt is
 * retried whenever the clock ticked in between, so no assertion can race a
 * second boundary. Tests whose verdict does not depend on the current time
 * (empty schedule, inverted range, wrong weekday, month masked out) are
 * written without that machinery.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Calendar.h"
#include "Utils.h"

//InPlageHoraire.h pulls src/lib/TimeRange.h in. Since T3.13 that header has an
//include guard, so reaching it through both paths is fine.
#include "TimeRange.h"
#include "InPlageHoraire.h"

#include "core/CalaosCoreFixture.h"

using Calaos::InPlageHoraire;

namespace
{

const long SECONDS_PER_DAY = 24 * 3600;

//Build an HTYPE_NORMAL range out of two seconds-of-day values.
TimeRange normalRange(long startSec, long endSec)
{
    TimeRange h;

    h.start_type = TimeRange::HTYPE_NORMAL;
    h.shour = Utils::to_string(startSec / 3600);
    h.smin = Utils::to_string((startSec % 3600) / 60);
    h.ssec = Utils::to_string(startSec % 60);

    h.end_type = TimeRange::HTYPE_NORMAL;
    h.ehour = Utils::to_string(endSec / 3600);
    h.emin = Utils::to_string((endSec % 3600) / 60);
    h.esec = Utils::to_string(endSec % 60);

    return h;
}

struct LocalNow
{
    int wday;       //0 = sunday, same convention as TimeRange::SUNDAY
    int mon;        //0 = january, same convention as InPlageHoraire::JANUARY
    long secOfDay;
};

//Pin the timezone of the process for the duration of a test, and restore it.
struct ScopedTimezone
{
    std::string saved;
    bool had = false;

    ScopedTimezone(const char *tz)
    {
        const char *cur = getenv("TZ");
        if (cur) { saved = cur; had = true; }

        setenv("TZ", tz, 1);
        tzset();
    }

    ~ScopedTimezone()
    {
        if (had) setenv("TZ", saved.c_str(), 1);
        else unsetenv("TZ");
        tzset();
    }
};

//A normalized struct tm for a local date and time, tm_wday included.
struct tm localDate(int year, int mon, int mday, int hour, int min)
{
    struct tm t;
    memset(&t, 0, sizeof(t));

    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_isdst = -1;

    mktime(&t); //normalize

    return t;
}

LocalNow localNow()
{
    tzset();
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);

    LocalNow n;
    n.wday = lt->tm_wday;
    n.mon = lt->tm_mon;
    n.secOfDay = lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec;

    return n;
}

}

/******************************************************************************
 * TimeRange - construction and conversion
 ******************************************************************************/

TEST(TimeRangeTest, DefaultRangeIsMidnightToMidnight)
{
    TimeRange h;

    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.start_type);
    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.end_type);
    EXPECT_EQ(0, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(0, h.getEndTimeSec(2025, 6, 15));

    //An empty range: start == end, the two bounds are the same instant
    EXPECT_TRUE(h.isSameStartEnd());
}

TEST(TimeRangeTest, NormalBoundsAreSecondsOfDay)
{
    TimeRange h = normalRange(8 * 3600 + 30 * 60 + 15, 20 * 3600 + 5 * 60 + 1);

    EXPECT_EQ(8 * 3600 + 30 * 60 + 15, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(20 * 3600 + 5 * 60 + 1, h.getEndTimeSec(2025, 6, 15));
    EXPECT_FALSE(h.isSameStartEnd());
}

TEST(TimeRangeTest, LastSecondOfDayIsInsideTheDay)
{
    //Midnight and the last second of the day: the two extremes a real
    //schedule can name.
    TimeRange h = normalRange(0, SECONDS_PER_DAY - 1);

    EXPECT_EQ(0, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(86399, h.getEndTimeSec(2025, 6, 15));
}

TEST(TimeRangeTest, HoursAreNotClampedToOneDay)
{
    //Characterizing the current behaviour: nothing validates the values, an
    //hour of 24 (or more) simply becomes a second count past the end of the
    //day, which no wall clock can ever reach. It is not rejected, and it does
    //not roll over to the next day either.
    TimeRange h = normalRange(0, 0);
    h.ehour = "24";

    EXPECT_EQ(SECONDS_PER_DAY, h.getEndTimeSec(2025, 6, 15));

    h.ehour = "25";
    h.emin = "70";
    EXPECT_EQ(25 * 3600 + 70 * 60, h.getEndTimeSec(2025, 6, 15));
}

TEST(TimeRangeTest, InvertedRangeKeepsBothBoundsAsIs)
{
    //end < start. TimeRange itself does not normalize anything, it is the
    //consumer that decides what an inverted range means (see
    //InPlageHoraireEvalTest.InvertedRangeWrapsOverMidnight).
    TimeRange h = normalRange(23 * 3600, 1 * 3600);

    EXPECT_EQ(23 * 3600, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(1 * 3600, h.getEndTimeSec(2025, 6, 15));
    EXPECT_GT(h.getStartTimeSec(2025, 6, 15), h.getEndTimeSec(2025, 6, 15));
    EXPECT_FALSE(h.isSameStartEnd());
}

TEST(TimeRangeTest, ProtoConstructorParsesEveryField)
{
    //day:shour:smin:ssec:start_type:start_offset:ehour:emin:esec:end_type:end_offset
    TimeRange h("3:8:30:15:0:1:20:05:01:0:1");

    EXPECT_EQ("8", h.shour);
    EXPECT_EQ("30", h.smin);
    EXPECT_EQ("15", h.ssec);
    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.start_type);
    EXPECT_EQ(1, h.start_offset);

    EXPECT_EQ("20", h.ehour);
    EXPECT_EQ("05", h.emin);
    EXPECT_EQ("01", h.esec);
    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.end_type);
    EXPECT_EQ(1, h.end_offset);

    EXPECT_EQ(8 * 3600 + 30 * 60 + 15, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(20 * 3600 + 5 * 60 + 1, h.getEndTimeSec(2025, 6, 15));
}

TEST(TimeRangeTest, ProtoCommandRoundTrip)
{
    const std::string proto = "3:8:30:15:0:1:20:5:1:0:1";

    TimeRange h(proto);
    EXPECT_EQ(proto, h.toProtoCommand(2)); //toProtoCommand() writes day + 1
}

TEST(TimeRangeTest, EmptyProtoDoesNotReadOutOfBounds)
{
    /* split() pads the token list up to its `max`, so a truncated (or empty)
     * proto string does not index past the end of the vector: every missing
     * field simply ends up empty.
     *
     * Before T3.13, getStartTimeSec()/getEndTimeSec() fed those empty strings
     * to from_string() into *uninitialized* locals and ignored its return
     * value, so the seconds-of-day of such a range was garbage (a negative
     * value was observed). It is now defined: an unparsable bound is reported
     * in the log, counted as 00:00:00, and isValid() says the range must be
     * ignored altogether.
     */
    TimeRange h("");

    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.start_type);
    EXPECT_EQ(TimeRange::HTYPE_NORMAL, h.end_type);
    EXPECT_EQ("", h.shour);
    EXPECT_EQ("", h.esec);

    //an unparsable offset is normalized to +1, like any non negative value
    EXPECT_EQ(1, h.start_offset);
    EXPECT_EQ(1, h.end_offset);

    EXPECT_FALSE(h.isValid());
    EXPECT_EQ(0, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(0, h.getEndTimeSec(2025, 6, 15));

    //a short proto string is padded the same way
    TimeRange truncated("1:8:30");
    EXPECT_EQ("8", truncated.shour);
    EXPECT_EQ("30", truncated.smin);
    EXPECT_EQ("", truncated.ssec);
    EXPECT_EQ(TimeRange::HTYPE_NORMAL, truncated.end_type);

    //the start bound is truncated too ("" seconds), so the whole range is out
    EXPECT_FALSE(truncated.isValid());
    EXPECT_EQ(0, truncated.getStartTimeSec(2025, 6, 15));
}

TEST(TimeRangeTest, IsValidRejectsBoundsThatAreNotNumbers)
{
    TimeRange h = normalRange(8 * 3600, 20 * 3600);
    EXPECT_TRUE(h.isValid());

    TimeRange empty = normalRange(8 * 3600, 20 * 3600);
    empty.emin = "";
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(0, empty.getEndTimeSec(2025, 6, 15)); //documented fallback

    TimeRange garbage = normalRange(8 * 3600, 20 * 3600);
    garbage.shour = "eight";
    EXPECT_FALSE(garbage.isValid());
    EXPECT_EQ(0, garbage.getStartTimeSec(2025, 6, 15));

    TimeRange trailing = normalRange(8 * 3600, 20 * 3600);
    trailing.ssec = "15s";
    EXPECT_FALSE(trailing.isValid());

    //a sunrise bound without offset never parses the strings as a time
    TimeRange sun;
    sun.start_type = TimeRange::HTYPE_SUNRISE;
    EXPECT_TRUE(sun.isValid());
}

TEST(TimeRangeTest, OffsetsAreNormalizedToPlusOrMinusOne)
{
    TimeRange plus("1:0:0:0:1:7:0:0:0:2:42");
    EXPECT_EQ(1, plus.start_offset);
    EXPECT_EQ(1, plus.end_offset);

    TimeRange minus("1:0:0:0:1:-7:0:0:0:2:-42");
    EXPECT_EQ(-1, minus.start_offset);
    EXPECT_EQ(-1, minus.end_offset);

    TimeRange zero("1:0:0:0:1:0:0:0:0:2:0");
    EXPECT_EQ(1, zero.start_offset); //0 is not negative -> +1
    EXPECT_EQ(1, zero.end_offset);
}

TEST(TimeRangeTest, ParamsConstructorAndToParamsRoundTrip)
{
    Params p;
    p.Add("start_hour", "8");
    p.Add("start_min", "30");
    p.Add("start_sec", "15");
    p.Add("start_type", Utils::to_string((int)TimeRange::HTYPE_NORMAL));
    p.Add("start_offset", "1");
    p.Add("end_hour", "20");
    p.Add("end_min", "5");
    p.Add("end_sec", "1");
    p.Add("end_type", Utils::to_string((int)TimeRange::HTYPE_NORMAL));
    p.Add("end_offset", "-1");

    TimeRange h(p);
    EXPECT_EQ(8 * 3600 + 30 * 60 + 15, h.getStartTimeSec(2025, 6, 15));
    EXPECT_EQ(20 * 3600 + 5 * 60 + 1, h.getEndTimeSec(2025, 6, 15));
    EXPECT_EQ(-1, h.end_offset);

    Params out = h.toParams(4);
    EXPECT_EQ("5", out["day"]); //toParams() writes day + 1, like toProtoCommand()
    for (int i = 0; i < p.size(); i++)
    {
        std::string key, value;
        p.get_item(i, key, value);
        EXPECT_EQ(value, out[key]) << "param " << key << " lost in the round trip";
    }

    //and the Params round trip rebuilds an equal range
    EXPECT_TRUE(TimeRange(out) == h);
}

TEST(TimeRangeTest, EqualityComparesEveryBound)
{
    TimeRange a = normalRange(8 * 3600, 20 * 3600);
    TimeRange b = normalRange(8 * 3600, 20 * 3600);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);

    b.ssec = "1";
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);

    TimeRange c = normalRange(8 * 3600, 20 * 3600);
    c.start_type = TimeRange::HTYPE_SUNRISE;
    EXPECT_TRUE(a != c);

    TimeRange d = normalRange(8 * 3600, 20 * 3600);
    d.end_offset = -1;
    EXPECT_TRUE(a != d);

    //dayOfWeek is UI sugar, it is deliberately not part of the comparison
    TimeRange e = normalRange(8 * 3600, 20 * 3600);
    e.dayOfWeek.set(TimeRange::MONDAY);
    EXPECT_TRUE(a == e);
}

TEST(TimeRangeTest, DayOfWeekBitsetSelectsIndividualDays)
{
    TimeRange h;

    EXPECT_EQ(0u, h.dayOfWeek.count()); //no day selected by default

    h.dayOfWeek.set(TimeRange::MONDAY);
    h.dayOfWeek.set(TimeRange::SATURDAY);

    EXPECT_EQ(2u, h.dayOfWeek.count());
    EXPECT_TRUE(h.dayOfWeek.test(TimeRange::MONDAY));
    EXPECT_TRUE(h.dayOfWeek.test(TimeRange::SATURDAY));
    EXPECT_FALSE(h.dayOfWeek.test(TimeRange::SUNDAY));
    EXPECT_FALSE(h.dayOfWeek.test(TimeRange::FRIDAY));

    //the weekday enum matches struct tm::tm_wday (sunday first)
    EXPECT_EQ(0, (int)TimeRange::SUNDAY);
    EXPECT_EQ(1, (int)TimeRange::MONDAY);
    EXPECT_EQ(6, (int)TimeRange::SATURDAY);
}

TEST(TimeRangeTest, ToStringDescribesBothBoundTypes)
{
    TimeRange h = normalRange(8 * 3600, 20 * 3600);
    const std::string s = h.toString();

    EXPECT_NE(std::string::npos, s.find("[Start: normal]"));
    EXPECT_NE(std::string::npos, s.find("[End: normal]"));

    TimeRange sun;
    sun.start_type = TimeRange::HTYPE_SUNRISE;
    sun.end_type = TimeRange::HTYPE_SUNSET;
    const std::string ss = sun.toString();

    EXPECT_NE(std::string::npos, ss.find("[Start: sunrise]"));
    EXPECT_NE(std::string::npos, ss.find("[End: sunset]"));
}

/******************************************************************************
 * InPlageHoraire - the day before, used to find the overnight ranges of
 * yesterday that are still running this morning
 ******************************************************************************/

TEST(InPlageHorairePreviousDayTest, WalksBackOneCalendarDay)
{
    ScopedTimezone tz("Europe/Paris");

    struct tm prev = InPlageHoraire::previousDay(localDate(2025, 6, 16, 0, 30));

    EXPECT_EQ(15, prev.tm_mday);
    EXPECT_EQ(5, prev.tm_mon); //june, 0 based
    EXPECT_EQ(125, prev.tm_year);
    EXPECT_EQ((int)TimeRange::SUNDAY, prev.tm_wday);
}

TEST(InPlageHorairePreviousDayTest, CrossesMonthYearAndLeapDay)
{
    ScopedTimezone tz("Europe/Paris");

    struct tm feb = InPlageHoraire::previousDay(localDate(2025, 3, 1, 0, 30));
    EXPECT_EQ(28, feb.tm_mday);
    EXPECT_EQ(1, feb.tm_mon);

    struct tm leap = InPlageHoraire::previousDay(localDate(2024, 3, 1, 0, 30));
    EXPECT_EQ(29, leap.tm_mday);
    EXPECT_EQ(1, leap.tm_mon);

    struct tm dec = InPlageHoraire::previousDay(localDate(2025, 1, 1, 0, 30));
    EXPECT_EQ(31, dec.tm_mday);
    EXPECT_EQ(11, dec.tm_mon);
    EXPECT_EQ(124, dec.tm_year);
}

TEST(InPlageHorairePreviousDayTest, IsNotFooledByASpringForward)
{
    /* In Europe/Paris, sunday 30 march 2025 is only 23h long (02:00 -> 03:00).
     * On the monday morning that follows, subtracting 24h from the time_t
     * lands on saturday the 29th, ie the day *before* yesterday: the overnight
     * ranges of sunday night would be missed and saturday's applied instead.
     * That is the whole reason previousDay() goes through struct tm.
     */
    ScopedTimezone tz("Europe/Paris");

    struct tm monday = localDate(2025, 3, 31, 0, 30);

    struct tm prev = InPlageHoraire::previousDay(monday);
    EXPECT_EQ(30, prev.tm_mday);
    EXPECT_EQ(2, prev.tm_mon);
    EXPECT_EQ((int)TimeRange::SUNDAY, prev.tm_wday);

    //what the naive time_t arithmetic gives on that very morning
    struct tm copy = monday;
    time_t t = mktime(&copy);
    time_t naive_t = t - 24 * 3600;
    struct tm naive = *localtime(&naive_t);

    if (naive.tm_mday == 30)
        GTEST_SKIP() << "no tzdata for Europe/Paris, the DST case cannot be exercised";

    EXPECT_EQ(29, naive.tm_mday); //the bug previousDay() does not have
}

/******************************************************************************
 * InPlageHoraire - evaluation
 ******************************************************************************/

class InPlageHoraireEvalTest: public CalaosTest::CoreFixture
{
protected:
    InPlageHoraire *plage = nullptr;

    void SetUp() override
    {
        CoreFixture::SetUp();

        //One empty room, no rule: this suite only needs a place to hang the IO
        loadConfig(CalaosTest::ioXmlDocument(
                       CalaosTest::roomXml("TimeRangeRoom", "office", std::string())),
                   CalaosTest::rulesXmlDocument(std::string()));

        static int counter = 0;
        const std::string id = "io_plage_" + Utils::to_string(++counter);

        Params p = { { "type", "InPlageHoraire" },
                     { "id", id },
                     { "name", "Plage" },
                     { "enabled", "true" } };

        plage = dynamic_cast<InPlageHoraire *>(createIO(p));
        ASSERT_NE(plage, nullptr) << "InPlageHoraire is not registered in this binary";
    }

    //Append the same range to the vector of one weekday.
    void addForWeekday(int wday, TimeRange h)
    {
        switch (wday)
        {
        case TimeRange::SUNDAY: plage->AddSunday(h); break;
        case TimeRange::MONDAY: plage->AddMonday(h); break;
        case TimeRange::TUESDAY: plage->AddTuesday(h); break;
        case TimeRange::WEDNESDAY: plage->AddWednesday(h); break;
        case TimeRange::THURSDAY: plage->AddThursday(h); break;
        case TimeRange::FRIDAY: plage->AddFriday(h); break;
        case TimeRange::SATURDAY: plage->AddSaturday(h); break;
        default: FAIL() << "bad weekday " << wday;
        }
    }

    void addForToday(TimeRange h) { addForWeekday(localNow().wday, h); }

    //The weekday of the day before today, ie the day an overnight range must
    //be attached to for its tail to be seen this morning.
    void addForYesterday(TimeRange h) { addForWeekday((localNow().wday + 6) % 7, h); }

    /* Evaluate a schedule that is expressed relative to the current second.
     * `build` receives the current second-of-day and fills the schedule;
     * `check` receives that same second and the resulting boolean value.
     * The whole thing is retried while the clock ticks during the evaluation,
     * so `check` is only ever called on a verdict computed with the very
     * second `build` was given.
     */
    void evalInStableWindow(std::function<void (long cur)> build,
                            std::function<void (long cur, bool value)> check)
    {
        for (int attempt = 0; attempt < 50; attempt++)
        {
            plage->clear();

            const long before = localNow().secOfDay;
            build(before);
            plage->hasChanged();
            const long after = localNow().secOfDay;

            if (before != after)
                continue; //the clock ticked mid-evaluation, inconclusive

            check(before, plage->get_value_bool());
            return;
        }

        GTEST_SKIP() << "could not evaluate the schedule within a single second";
    }
};

TEST_F(InPlageHoraireEvalTest, EmptyScheduleIsFalse)
{
    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, FullDayRangeOnTodayIsTrue)
{
    addForToday(normalRange(0, SECONDS_PER_DAY - 1));

    plage->hasChanged();
    EXPECT_TRUE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, FullDayRangeOnAnotherWeekdayIsFalse)
{
    //Every weekday but today: only the vector of the current weekday is read
    const int today = localNow().wday;
    for (int d = 0; d < 7; d++)
    {
        if (d == today) continue;
        addForWeekday(d, normalRange(0, SECONDS_PER_DAY - 1));
    }

    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());

    //...and adding today's makes it true, so the difference really is the day
    addForWeekday(today, normalRange(0, SECONDS_PER_DAY - 1));
    plage->hasChanged();
    EXPECT_TRUE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, InvertedRangeWrapsOverMidnight)
{
    /* CONTRACT (T3.13, user decision). This test used to be
     * InvertedRangeNeverMatches and pinned the opposite: a range whose end was
     * before its start (23:00 -> 01:00, the natural way of writing "over
     * midnight") was an empty range that never matched. It now wraps.
     *
     * The range is attached to *today* only, and an overnight range belongs to
     * the evening of the day it is attached to: it covers [23:00, end of day]
     * today and would cover [00:00, 01:00] tomorrow. So at any moment before
     * 23:00 - this morning included - it is false; the 00:00 -> 01:00 tail
     * seen this morning belongs to yesterday's copy of the range, see
     * PreviousDayOvernightRangeSpillsIntoThisMorning.
     */
    evalInStableWindow(
        [this](long) { addForToday(normalRange(23 * 3600, 1 * 3600)); },
        [](long cur, bool value) { EXPECT_EQ(cur >= 23 * 3600, value); });
}

TEST_F(InPlageHoraireEvalTest, OvernightRangeIsFalseInTheGapBetweenEndAndStart)
{
    //An overnight range [start, end] with end < start is false exactly on
    //(end, start): here the hole is centered on the current second.
    evalInStableWindow(
        [this](long cur)
        {
            if (cur < 1 || cur > SECONDS_PER_DAY - 2) return;
            addForToday(normalRange(cur + 1, cur - 1));
        },
        [](long cur, bool value)
        {
            if (cur < 1 || cur > SECONDS_PER_DAY - 2) return;
            EXPECT_FALSE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, OvernightRangeDoesNotMatchTheMorningOfItsOwnDay)
{
    /* The weekday half of the decision: "monday 23:00 -> 01:00" means monday
     * 23:00 up to tuesday 01:00 (continuity of monday night). It is NOT read
     * as "monday 00:00 -> 01:00 plus monday 23:00 -> 24:00", so the tail must
     * not be visible on the morning of the very day the range is attached to.
     */
    evalInStableWindow(
        [this](long cur)
        {
            if (cur >= SECONDS_PER_DAY - 1) return;
            //ends now, but starts at the last second of the day
            addForToday(normalRange(SECONDS_PER_DAY - 1, cur));
        },
        [](long cur, bool value)
        {
            if (cur >= SECONDS_PER_DAY - 1) return;
            EXPECT_FALSE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, PreviousDayOvernightRangeSpillsIntoThisMorning)
{
    //Same range as above, attached to yesterday: its tail is what covers the
    //current second.
    evalInStableWindow(
        [this](long cur)
        {
            if (cur >= SECONDS_PER_DAY - 1) return;
            addForYesterday(normalRange(SECONDS_PER_DAY - 1, cur));
        },
        [](long cur, bool value)
        {
            if (cur >= SECONDS_PER_DAY - 1) return;
            EXPECT_TRUE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, PreviousDayOvernightRangeStopsAtItsEnd)
{
    //...and it stops at its end: one second earlier and this morning is out.
    evalInStableWindow(
        [this](long cur)
        {
            if (cur < 1 || cur >= SECONDS_PER_DAY - 1) return;
            addForYesterday(normalRange(SECONDS_PER_DAY - 1, cur - 1));
        },
        [](long cur, bool value)
        {
            if (cur < 1 || cur >= SECONDS_PER_DAY - 1) return;
            EXPECT_FALSE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, PreviousDayPlainRangeDoesNotSpill)
{
    //Only an overnight (end < start) range crosses midnight. A plain range of
    //the previous day stays in the previous day, even a full day one.
    addForYesterday(normalRange(0, SECONDS_PER_DAY - 1));

    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, RangeWithAnUnparsableBoundIsIgnored)
{
    /* A bound that does not parse falls back to 00:00:00 (see
     * TimeRangeTest.IsValidRejectsBoundsThatAreNotNumbers). Evaluating it
     * anyway would turn a full day range into an inverted one and make it wrap
     * for no reason, so such a range is skipped entirely.
     */
    TimeRange broken = normalRange(0, SECONDS_PER_DAY - 1);
    broken.emin = ""; //as a truncated proto command would leave it

    addForToday(broken);
    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());

    //and a valid range next to it is still evaluated
    addForToday(normalRange(0, SECONDS_PER_DAY - 1));
    plage->hasChanged();
    EXPECT_TRUE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, BoundsAreInclusive)
{
    //A degenerate range [t, t] matches at exactly t: both bounds are closed.
    evalInStableWindow(
        [this](long cur) { addForToday(normalRange(cur, cur)); },
        [](long, bool value) { EXPECT_TRUE(value); });
}

TEST_F(InPlageHoraireEvalTest, RangeEndingOneSecondBeforeNowIsFalse)
{
    evalInStableWindow(
        [this](long cur)
        {
            if (cur < 2) return; //just after midnight, nothing to test
            addForToday(normalRange(0, cur - 1));
        },
        [](long cur, bool value)
        {
            if (cur < 2) return;
            EXPECT_FALSE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, RangeStartingOneSecondAfterNowIsFalse)
{
    evalInStableWindow(
        [this](long cur)
        {
            if (cur > SECONDS_PER_DAY - 3) return; //just before midnight
            addForToday(normalRange(cur + 1, SECONDS_PER_DAY - 1));
        },
        [](long cur, bool value)
        {
            if (cur > SECONDS_PER_DAY - 3) return;
            EXPECT_FALSE(value);
        });
}

TEST_F(InPlageHoraireEvalTest, MidnightOnlyRangeMatchesOnlyAtMidnight)
{
    //[00:00:00, 00:00:00] is true for exactly one second a day. Day rollover
    //seen from the other side: the range does not leak into the previous day.
    evalInStableWindow(
        [this](long) { addForToday(normalRange(0, 0)); },
        [](long cur, bool value) { EXPECT_EQ(cur == 0, value); });
}

TEST_F(InPlageHoraireEvalTest, SeveralRangesAreUnioned)
{
    //One overnight range, true only late in the evening, plus one that covers
    //the whole day: the value is the union, true at any hour.
    evalInStableWindow(
        [this](long)
        {
            addForToday(normalRange(23 * 3600, 1 * 3600)); //overnight
            addForToday(normalRange(0, SECONDS_PER_DAY - 1));
        },
        [](long, bool value) { EXPECT_TRUE(value); });
}

TEST_F(InPlageHoraireEvalTest, MonthNotSelectedForcesFalse)
{
    const LocalNow now = localNow();

    addForToday(normalRange(0, SECONDS_PER_DAY - 1));
    plage->hasChanged();
    ASSERT_TRUE(plage->get_value_bool());

    //Every month is set by the constructor; drop the current one
    plage->months.reset(now.mon);
    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());

    plage->months.set(now.mon);
    plage->hasChanged();
    EXPECT_TRUE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, ValueGoesBackToFalseWhenTheScheduleIsCleared)
{
    addForToday(normalRange(0, SECONDS_PER_DAY - 1));
    plage->hasChanged();
    ASSERT_TRUE(plage->get_value_bool());

    plage->clear();
    EXPECT_TRUE(plage->getMonday().empty());
    EXPECT_TRUE(plage->getSunday().empty());

    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, DisabledIoKeepsItsValue)
{
    addForToday(normalRange(0, SECONDS_PER_DAY - 1));
    plage->hasChanged();
    ASSERT_TRUE(plage->get_value_bool());

    //A disabled IO is not re-evaluated at all: the last known value stays
    plage->set_param("enabled", "false");
    plage->clear();
    plage->hasChanged();
    EXPECT_TRUE(plage->get_value_bool());

    plage->set_param("enabled", "true");
    plage->hasChanged();
    EXPECT_FALSE(plage->get_value_bool());
}

TEST_F(InPlageHoraireEvalTest, TypeAndDefaultParams)
{
    EXPECT_EQ(Utils::TBOOL, plage->get_type());

    //A time range has no UI representation of its own
    EXPECT_EQ("false", plage->get_param("visible"));
    EXPECT_EQ("time_range", plage->get_param("gui_type"));

    //every month active by default
    EXPECT_EQ(12u, plage->months.count());
}

TEST_F(InPlageHoraireEvalTest, SettersReplaceTheWholeDayVector)
{
    std::vector<TimeRange> v;
    v.push_back(normalRange(8 * 3600, 12 * 3600));
    v.push_back(normalRange(14 * 3600, 18 * 3600));

    plage->setWednesday(v);
    ASSERT_EQ(2u, plage->getWednesday().size());
    EXPECT_TRUE(plage->getWednesday()[0] == v[0]);
    EXPECT_TRUE(plage->getWednesday()[1] == v[1]);

    std::vector<TimeRange> single;
    single.push_back(normalRange(0, 1));
    plage->setWednesday(single);
    EXPECT_EQ(1u, plage->getWednesday().size());

    //the other days are untouched
    EXPECT_TRUE(plage->getThursday().empty());
}

/******************************************************************************
 * Calendar - only what CommonLib_test.cpp does not already cover
 ******************************************************************************/

TEST(CalendarRolloverTest, FebruaryLengthFollowsTheLeapYearRule)
{
    Calendar cal;
    cal.setDay(1);

    cal.setMonth(2);

    cal.setYear(2024);
    EXPECT_EQ(29, cal.getNbDaysInMonth()); //divisible by 4

    cal.setYear(2025);
    EXPECT_EQ(28, cal.getNbDaysInMonth());

    cal.setYear(2000);
    EXPECT_EQ(29, cal.getNbDaysInMonth()); //divisible by 400

    cal.setYear(1900);
    EXPECT_EQ(28, cal.getNbDaysInMonth()); //divisible by 100 but not by 400
}

TEST(CalendarRolloverTest, MonthLengthsOutsideFebruary)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setDay(1);

    const int expected[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (int m = 1; m <= 12; m++)
    {
        cal.setMonth(m);
        EXPECT_EQ(expected[m], cal.getNbDaysInMonth()) << "month " << m;
    }

    //out of range months have no length at all
    cal.setMonth(13);
    EXPECT_EQ(0, cal.getNbDaysInMonth());
}

TEST(CalendarRolloverTest, SetMonthClampsTheDayToTheNewMonth)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setMonth(1);
    cal.setDay(31);

    cal.setMonth(2);
    EXPECT_EQ(2, cal.month);
    EXPECT_EQ(28, cal.day); //31 january -> 28 february

    cal.setDay(30);
    cal.setMonth(4);
    EXPECT_EQ(30, cal.day); //april has 30 days, nothing to clamp
}

TEST(CalendarRolloverTest, SetYearClampsFebruary29OnANonLeapYear)
{
    Calendar cal;
    cal.setYear(2024);
    cal.setMonth(2);
    cal.setDay(29);
    ASSERT_EQ(29, cal.day);

    cal.setYear(2025);
    EXPECT_EQ(28, cal.day);
}

TEST(CalendarRolloverTest, DayUpWrapsToTheFirstOfTheSameMonth)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setMonth(4);
    cal.setDay(29);

    cal.dayUp();
    EXPECT_EQ(30, cal.day);

    //CHARACTERIZING: the day wraps inside the month, the month is not bumped
    cal.dayUp();
    EXPECT_EQ(1, cal.day);
    EXPECT_EQ(4, cal.month);
}

TEST(CalendarRolloverTest, HoursMinutesAndSecondsWrapAround)
{
    Calendar cal;

    cal.setHours(23);
    cal.hoursUp();
    EXPECT_EQ(0, cal.hours);
    cal.hoursDec();
    EXPECT_EQ(23, cal.hours);

    cal.setMinutes(59);
    cal.minutesUp();
    EXPECT_EQ(0, cal.minutes);
    cal.minutesDec();
    EXPECT_EQ(59, cal.minutes);

    cal.setSecondes(59);
    cal.secondesUp();
    EXPECT_EQ(0, cal.secondes);
    cal.secondesDec();
    EXPECT_EQ(59, cal.secondes);
}

TEST(CalendarRolloverTest, TimeStringsAreZeroPadded)
{
    Calendar cal;

    cal.setHours(7);
    cal.setMinutes(5);
    cal.setSecondes(0);
    EXPECT_EQ("07", cal.hoursToString());
    EXPECT_EQ("05", cal.minutesToString());
    EXPECT_EQ("00", cal.secondesToString());

    cal.setHours(23);
    cal.setMinutes(45);
    cal.setSecondes(59);
    EXPECT_EQ("23", cal.hoursToString());
    EXPECT_EQ("45", cal.minutesToString());
    EXPECT_EQ("59", cal.secondesToString());
}

TEST(CalendarRolloverTest, MonthNameFollowsTheMonthNumber)
{
    Calendar cal;
    cal.setYear(2025);
    cal.setDay(1);

    cal.setMonth(1);
    EXPECT_EQ("Janvier", cal.getMonthFromDate());
    cal.setMonth(12);
    EXPECT_EQ("Decembre", cal.getMonthFromDate());

    //out of range: the empty placeholder, never an out of bounds read
    cal.setMonth(0);
    EXPECT_EQ("", cal.getMonthFromDate());
    cal.setMonth(13);
    EXPECT_EQ("", cal.getMonthFromDate());
}
