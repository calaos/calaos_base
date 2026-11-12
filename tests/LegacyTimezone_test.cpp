#include <gtest/gtest.h>
#include "RemoteUI/LegacyTimezone.h"

using Calaos::LegacyTimezone::normalizeConfig;
using Calaos::LegacyTimezone::normalizePages;
using Calaos::LegacyTimezone::toPosix;

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
    EXPECT_EQ("UTCx123", toPosix("UTCx123"));
    EXPECT_EQ("UTC+:300", toPosix("UTC+:300"));
}

TEST(LegacyTimezone, PagesOnlyClockTimezoneRewrittenInCopy)
{
    using Json = nlohmann::json;
    Json clockUtc1 = Json{ { "type", "clock" }, { "clock_timezone", "UTC+1" } };
    Json clockUtc0 = Json{ { "type", "clock" }, { "clock_timezone", "UTC0" } };
    Json light = Json{ { "type", "light" } };
    Json pages = Json::array({ Json{ { "widgets", Json::array({ clockUtc1, clockUtc0, light }) } } });
    Json out = normalizePages(pages);
    EXPECT_EQ("<+01>-1", out[0]["widgets"][0]["clock_timezone"]);
    EXPECT_EQ("UTC0", out[0]["widgets"][1]["clock_timezone"]);
    EXPECT_FALSE(out[0]["widgets"][2].contains("clock_timezone"));
    EXPECT_EQ("UTC+1", pages[0]["widgets"][0]["clock_timezone"]);
    EXPECT_TRUE(normalizePages(Json::object()).is_object());
}

TEST(LegacyTimezone, MalformedPagesAndWidgetsSkipped)
{
    using Json = nlohmann::json;
    Json pages = Json::array({ 42, Json::object(), Json{ { "widgets", 1 } },
                               Json{ { "widgets", Json::array({ 7, Json{ { "clock_timezone", 5 } } }) } } });
    EXPECT_EQ(pages, normalizePages(pages));
}

TEST(LegacyTimezone, NormalizeConfig)
{
    using Json = nlohmann::json;
    Json data = Json{ { "screensaver_clock_timezone", "UTC-5" },
                      { "pages", Json::array({ Json{ { "widgets", Json::array({ Json{ { "clock_timezone", "UTC+1" } } }) } } }) } };
    normalizeConfig(data);
    EXPECT_EQ("<-05>5", data["screensaver_clock_timezone"]);
    EXPECT_EQ("<+01>-1", data["pages"][0]["widgets"][0]["clock_timezone"]);

    Json empty = Json::object();
    normalizeConfig(empty);
    EXPECT_TRUE(empty.empty());
}
