#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include "RemoteUI/LegacyTimezone.h"

using Calaos::LegacyTimezone::normalizeConfig;
using Calaos::LegacyTimezone::normalizePages;
using Calaos::LegacyTimezone::toPosix;
using Json = nlohmann::json;

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

TEST(LegacyTimezone, NamedOffsetsAreQuoted)
{
    EXPECT_EQ("<UTC+1>-1<UTC+2>,M3.5.0,M10.5.0/3", toPosix("UTC+1-1UTC+2,M3.5.0,M10.5.0/3"));
    EXPECT_EQ("<UTC-5>+5<UTC-4>,M3.2.0,M11.1.0", toPosix("UTC-5+5UTC-4,M3.2.0,M11.1.0"));
    EXPECT_EQ("<UTC+5:30>-5:30", toPosix("UTC+5:30-5:30"));
    EXPECT_EQ("<UTC+1>-1<UTC+2>-2,M3.5.0,M10.5.0/3", toPosix("UTC+1-1UTC+2-2,M3.5.0,M10.5.0/3"));
}

TEST(LegacyTimezone, ValidPosixIsNeverRewritten)
{
    for (const char *tz: { "CET-1CEST,M3.5.0,M10.5.0/3", "EST5EDT,M3.2.0,M11.1.0", "<+01>-1", "<UTC+1>-1<UTC+2>,M3.5.0,M10.5.0/3",
                           "UTC0", "CET-1", "AEST-10AEDT,M10.1.0,M4.1.0/3", "UTC+1-1UTC", "UTC+1-1UTC+2x" })
        EXPECT_EQ(tz, toPosix(tz)) << tz;
}

namespace
{
//Minimal TZif: header + empty v1 block + v2 header + footer. Only the magic,
//the version byte and the trailing "\n<rule>\n" matter to the reader.
void writeTzif(const std::string &path, char version, const std::string &footer)
{
    std::ofstream f(path, std::ios::binary);
    std::string hdr = std::string("TZif") + version + std::string(15, '\0');
    f << hdr << std::string(24, '\0') << hdr << std::string(24, '\0');
    if (version >= '2')
        f << "\n"
          << footer << "\n";
}
} //namespace

class IanaTimezone: public ::testing::Test
{
protected:
    std::string root;
    void SetUp() override
    {
        char tmpl[] = "/tmp/tzroot.XXXXXX";
        ASSERT_NE(nullptr, mkdtemp(tmpl));
        root = tmpl;
        ASSERT_EQ(0, system(("mkdir -p " + root + "/Europe " + root + "/America " + root + "/Asia").c_str()));
        writeTzif(root + "/Europe/Paris", '2', "CET-1CEST,M3.5.0,M10.5.0/3");
        writeTzif(root + "/America/New_York", '3', "EST5EDT,M3.2.0,M11.1.0");
        writeTzif(root + "/Asia/Kolkata", '2', "IST-5:30");
        writeTzif(root + "/Europe/Old", '1', "");
        writeTzif(root + "/Europe/NoRule", '2', "");
        writeTzif(root + "/outside", '2', "OUT0");
        std::ofstream(root + "/Europe/Garbage") << "not a tzif file\n";
    }
    void TearDown() override
    {
        if (!root.empty() && root.rfind("/tmp/tzroot.", 0) == 0)
            ASSERT_EQ(0, system(("rm -rf " + root).c_str()));
    }
};

TEST_F(IanaTimezone, FooterOfAValidZone)
{
    using Calaos::LegacyTimezone::posixFromIana;
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", posixFromIana("Europe/Paris", root));
    EXPECT_EQ("EST5EDT,M3.2.0,M11.1.0", posixFromIana("America/New_York", root));
    EXPECT_EQ("IST-5:30", posixFromIana("Asia/Kolkata", root));
}

TEST_F(IanaTimezone, InvalidOrTrappedNamesGiveNothing)
{
    using Calaos::LegacyTimezone::posixFromIana;
    for (const char *z: { "", "../outside", "Europe/../outside", "/etc/passwd", "/outside", "Europe/Missing", "Europe/", "Europe",
                          "Europe/Old", "Europe/NoRule", "Europe/Garbage", "Europe/Paris\n", "Europe/Pa ris", "Europe/Paris;x" })
        EXPECT_EQ("", posixFromIana(z, root)) << z;
}

TEST_F(IanaTimezone, IanaWinsOverTheStoredPosix)
{
    using Calaos::LegacyTimezone::resolve;
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", resolve("Europe/Paris", "UTC+1-1UTC+2,M3.5.0,M10.5.0/3", root));
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", resolve("Europe/Paris", "", root));
    EXPECT_EQ("<+01>-1", resolve("Europe/Missing", "UTC+1", root));
    EXPECT_EQ("<UTC+1>-1<UTC+2>,M3.5.0,M10.5.0/3", resolve("../x", "UTC+1-1UTC+2,M3.5.0,M10.5.0/3", root));
    EXPECT_EQ("", resolve("", "", root));
}

TEST_F(IanaTimezone, WidgetsAndScreensaver)
{
    Json pages = Json::array({ Json{ { "widgets", Json::array({ Json{ { "clock_timezone", "UTC+1-1UTC+2,M3.5.0,M10.5.0/3" }, { "clock_timezone_iana", "Europe/Paris" } },
                                                                Json{ { "clock_timezone_iana", "America/New_York" } },
                                                                Json{ { "clock_timezone", "UTC+1" }, { "clock_timezone_iana", "Nowhere/Land" } },
                                                                Json{ { "type", "clock" } } }) } } });
    Json out = normalizePages(pages, root);
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", out[0]["widgets"][0]["clock_timezone"]);
    EXPECT_EQ("EST5EDT,M3.2.0,M11.1.0", out[0]["widgets"][1]["clock_timezone"]);
    EXPECT_EQ("<+01>-1", out[0]["widgets"][2]["clock_timezone"]);
    EXPECT_FALSE(out[0]["widgets"][3].contains("clock_timezone"));
    EXPECT_EQ("UTC+1-1UTC+2,M3.5.0,M10.5.0/3", pages[0]["widgets"][0]["clock_timezone"]);

    Json data = Json{ { "screensaver_clock_timezone", "UTC+1-1UTC+2,M3.5.0,M10.5.0/3" }, { "pages", pages } };
    normalizeConfig(data, "Asia/Kolkata", root);
    EXPECT_EQ("IST-5:30", data["screensaver_clock_timezone"]);

    Json noKey = Json{ { "pages", Json::array() } };
    normalizeConfig(noKey, "Europe/Paris", root);
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", noKey["screensaver_clock_timezone"]);
    Json untouched = Json{ { "pages", Json::array() } };
    normalizeConfig(untouched, "", root);
    EXPECT_FALSE(untouched.contains("screensaver_clock_timezone"));
}

TEST(IanaTimezoneSystem, RealZoneinfoWhenInstalled)
{
    using Calaos::LegacyTimezone::posixFromIana;
    if (access("/usr/share/zoneinfo/Europe/Paris", R_OK) != 0)
        GTEST_SKIP() << "no tzdata in this image";
    EXPECT_EQ("CET-1CEST,M3.5.0,M10.5.0/3", posixFromIana("Europe/Paris"));
    EXPECT_EQ("EST5EDT,M3.2.0,M11.1.0", posixFromIana("America/New_York"));
    EXPECT_EQ("IST-5:30", posixFromIana("Asia/Kolkata"));
}
