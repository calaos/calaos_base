#include <gtest/gtest.h>
#include "RemoteUI/LegacyTimezone.h"

using Calaos::LegacyTimezone::toPosix;
using Calaos::LegacyTimezone::normalizePages;

TEST(LegacyTimezone, PositiveOffsets)
{
    EXPECT_EQ("<+01>-1", toPosix("UTC+1"));
    EXPECT_EQ("<+14>-14", toPosix("UTC+14"));
    EXPECT_EQ("<+01>-1", toPosix("GMT+1"));
}

TEST(LegacyTimezone, NegativeOffsets)
{
    EXPECT_EQ("<-05>5", toPosix("UTC-5"));
    EXPECT_EQ("<-12>12", toPosix("UTC-12"));
}

TEST(LegacyTimezone, HalfHours)
{
    EXPECT_EQ("<+0530>-5:30", toPosix("UTC+5:30"));
    EXPECT_EQ("<+0330>-3:30", toPosix("UTC+3:30"));
    EXPECT_EQ("<-0930>9:30", toPosix("UTC-9:30"));
    EXPECT_EQ("<+0545>-5:45", toPosix("UTC+5:45"));
}

TEST(LegacyTimezone, ZeroOffset)
{
    EXPECT_EQ("UTC0", toPosix("UTC+0"));
    EXPECT_EQ("UTC0", toPosix("UTC-0"));
}

TEST(LegacyTimezone, PosixAndOtherValuesUntouched)
{
    EXPECT_EQ("", toPosix(""));
    EXPECT_EQ("UTC", toPosix("UTC"));
    EXPECT_EQ("UTC0", toPosix("UTC0"));
    EXPECT_EQ("UTC-1x", toPosix("UTC-1x"));
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", toPosix("CET-1CEST,M3.5.0,M10.5.0/3"));
    EXPECT_EQ("<+01>-1", toPosix(toPosix("UTC+1")));
    EXPECT_EQ("Europe/Paris", toPosix("Europe/Paris"));
    EXPECT_EQ("UTC+15", toPosix("UTC+15"));
    EXPECT_EQ("UTC+5:75", toPosix("UTC+5:75"));
    EXPECT_EQ("UTC+5:3", toPosix("UTC+5:3"));
    EXPECT_EQ("UTC+", toPosix("UTC+"));
}

TEST(LegacyTimezone, PagesOnlyClockTimezoneRewrittenInCopy)
{
    using Json = nlohmann::json;
    Json pages = Json::array({ Json{ { "widgets", Json::array({
        Json{ { "type", "clock" }, { "clock_timezone", "UTC+1" } },
        Json{ { "type", "clock" }, { "clock_timezone", "UTC0" } },
        Json{ { "type", "light" } } }) } } });
    Json out = normalizePages(pages);
    EXPECT_EQ("<+01>-1", out[0]["widgets"][0]["clock_timezone"]);
    EXPECT_EQ("UTC0", out[0]["widgets"][1]["clock_timezone"]);
    EXPECT_FALSE(out[0]["widgets"][2].contains("clock_timezone"));
    EXPECT_EQ("UTC+1", pages[0]["widgets"][0]["clock_timezone"]);
    EXPECT_TRUE(normalizePages(Json::object()).is_object());
}
