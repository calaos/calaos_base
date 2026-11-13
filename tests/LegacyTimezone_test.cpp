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
        ASSERT_EQ(0, system(("mkdir -p " + root + "/Europe " + root + "/America " + root + "/Asia " + root + "/Etc").c_str()));
        writeTzif(root + "/Europe/Paris", '2', "CET-1CEST,M3.5.0,M10.5.0/3");
        writeTzif(root + "/America/New_York", '3', "EST5EDT,M3.2.0,M11.1.0");
        writeTzif(root + "/Asia/Kolkata", '2', "IST-5:30");
        writeTzif(root + "/Etc/GMT-14", '2', "<+14>-14");
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
    EXPECT_EQ("<+14>-14", posixFromIana("Etc/GMT-14", root));
}

// The installer's pseudo-IANA carries the POSIX sign (UTC-15 is 15 h east, as in
// its convertStoredTimezone("UTC+15") == "UTC-15"), so it is relayed unchanged.
TEST_F(IanaTimezone, ZonelessOffsetInIanaIsRelayedAsPosix)
{
    using Calaos::LegacyTimezone::ianaToPosix;
    using Calaos::LegacyTimezone::resolve;
    for (const char *z: { "UTC-15", "UTC+13", "UTC-4:15", "UTC+5:30", "UTC-13:30", "UTC+0", "UTC-24", "UTC13", "UTC3:30", "UTC0", "UTC24" })
    {
        EXPECT_EQ(z, ianaToPosix(z, root)) << z;
        EXPECT_EQ(z, resolve(z, "", root)) << z;
    }
    EXPECT_EQ("UTC-15", resolve("UTC-15", "UTC+15", root));
    EXPECT_EQ("<+14>-14", ianaToPosix("Etc/GMT-14", root));
    for (const char *z: { "UTC", "UTC+", "UTC+x", "UTC+25", "UTC+123", "UTC+5:", "UTC+5:3", "UTC+5:60", "UTC+5:300", "UTC+5:30x", "UTC+5;1", "GMT+5", "UTC25", "UTC123", "UTC3:", "UTC3:60", "UTC3:30x", "UTC3x", "UTC++3", "Europe/Paris:30" })
        EXPECT_EQ("", ianaToPosix(z, root)) << z;
}

TEST_F(IanaTimezone, ZonelessOffsetReachesWidgetsAndScreensaver)
{
    Json pages = Json::array({ Json{ { "widgets", Json::array({ Json{ { "clock_timezone_iana", "UTC-15" } },
                                                                Json{ { "clock_timezone_iana", "UTC+5:30" } },
                                                                Json{ { "clock_timezone", "UTC+1" }, { "clock_timezone_iana", "UTC-4:15" } } }) } } });
    Json out = normalizePages(pages, root);
    EXPECT_EQ("UTC-15", out[0]["widgets"][0]["clock_timezone"]);
    EXPECT_EQ("UTC+5:30", out[0]["widgets"][1]["clock_timezone"]);
    EXPECT_EQ("UTC-4:15", out[0]["widgets"][2]["clock_timezone"]);
    EXPECT_FALSE(pages[0]["widgets"][0].contains("clock_timezone"));

    Json west = Json::array({ Json{ { "widgets", Json::array({ Json{ { "clock_timezone_iana", "UTC13" } },
                                                               Json{ { "clock_timezone", "UTC+1" }, { "clock_timezone_iana", "UTC3:30" } } }) } } });
    Json wout = normalizePages(west, root);
    EXPECT_EQ("UTC13", wout[0]["widgets"][0]["clock_timezone"]);
    EXPECT_EQ("UTC3:30", wout[0]["widgets"][1]["clock_timezone"]);

    Json westNoKey = Json{ { "pages", Json::array() } };
    normalizeConfig(westNoKey, "UTC3:30", root);
    EXPECT_EQ("UTC3:30", westNoKey["screensaver_clock_timezone"]);
    Json westKey = Json{ { "screensaver_clock_timezone", "UTC-15" }, { "pages", Json::array() } };
    normalizeConfig(westKey, "UTC13", root);
    EXPECT_EQ("UTC13", westKey["screensaver_clock_timezone"]);

    Json noKey = Json{ { "pages", pages } };
    normalizeConfig(noKey, "UTC+5:30", root);
    EXPECT_EQ("UTC+5:30", noKey["screensaver_clock_timezone"]);
    Json withKey = Json{ { "screensaver_clock_timezone", "UTC-15" }, { "pages", Json::array() } };
    normalizeConfig(withKey, "UTC-13", root);
    EXPECT_EQ("UTC-13", withKey["screensaver_clock_timezone"]);
    Json fallback = Json{ { "screensaver_clock_timezone", "UTC+1" }, { "pages", Json::array() } };
    normalizeConfig(fallback, "UTC+5:", root);
    EXPECT_EQ("<+01>-1", fallback["screensaver_clock_timezone"]);
}

TEST_F(IanaTimezone, InvalidOrTrappedNamesGiveNothing)
{
    using Calaos::LegacyTimezone::posixFromIana;
    for (const char *z: { "", "../outside", "Europe/../outside", "/etc/passwd", "/outside", "Europe/Missing", "Europe/", "Europe",
                          "Europe/Old", "Europe/NoRule", "Europe/Garbage", "Europe/Paris\n", "Europe/Pa ris", "Europe/Paris;x" })
        EXPECT_EQ("", posixFromIana(z, root)) << z;
}

TEST_F(IanaTimezone, TruncatedOrMalformedFootersGiveNothing)
{
    using Calaos::LegacyTimezone::posixFromIana;
    auto raw = [&](const char *name, const std::string &bytes)
    {
        std::ofstream(root + "/Europe/" + name, std::ios::binary) << bytes;
    };
    raw("Tiny", "TZif2");
    raw("NoNewline", std::string("TZif2") + std::string(20, 'x'));
    raw("OneLine", std::string("TZif2") + std::string(20, 'x') + "\n");
    raw("Control", std::string("TZif2") + std::string(15, '\0') + "\nCET\x01-1\n");
    for (const char *z: { "Europe/Tiny", "Europe/NoNewline", "Europe/OneLine", "Europe/Control" })
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
