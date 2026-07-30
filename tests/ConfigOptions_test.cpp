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

/* Invariants of the local_config.xml option registry.
 *
 * These tests are the guard rail of the registry: adding an option without a
 * label or a documentation, with a default its own validate() refuses, with a
 * one-way seeAlso() or with a key that is also declared obsolete breaks
 * "make check" instead of silently shipping a half-declared option.
 */

#include "ConfigOptions.h"
#include "Utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "json.hpp"

using namespace Calaos;

typedef ConfigOption::Category C;
typedef ConfigOption::Type T;

//Every value of ConfigOption::Type. Adding a type without adding it here makes
//the type label test fail, which is the point.
static std::vector<T> allTypes()
{
    return { T::String, T::Bool, T::Int, T::Float, T::Port, T::Host, T::Path,
             T::Email, T::EmailList, T::Password, T::Token, T::Enum, T::Csv };
}

static const ConfigOption &opt(const std::string &key)
{
    const ConfigOption *o = ConfigOptions::find(key);
    EXPECT_NE(o, nullptr) << "option " << key << " is missing from the registry";
    static ConfigOption dummy("", C::Unknown, T::String);
    return o ? *o : dummy;
}

//-----------------------------------------------------------------------------
// Table structure
//-----------------------------------------------------------------------------

TEST(ConfigOptionsTable, IsNotEmpty)
{
    EXPECT_GT(ConfigOptions::all().size(), 40u);
}

TEST(ConfigOptionsTable, NoDuplicateKey)
{
    std::set<std::string> seen;
    for (const ConfigOption &o: ConfigOptions::all())
    {
        EXPECT_FALSE(o.key().empty()) << "an option has an empty key";
        EXPECT_TRUE(seen.insert(o.key()).second) << "duplicate key: " << o.key();
    }
}

//The coverage guard: no option may ship without a label and a description.
TEST(ConfigOptionsTable, EveryOptionIsDocumented)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        EXPECT_FALSE(o.label().empty()) << o.key() << " has no label()";
        EXPECT_FALSE(o.doc().empty()) << o.key() << " has no doc()";
        //A one word "documentation" is not one
        EXPECT_GT(o.doc().size(), 30u) << o.key() << " has a placeholder doc()";
    }
}

TEST(ConfigOptionsTable, EveryOptionHasAConsumer)
{
    const int known = ConfigOption::Server | ConfigOption::CalaosHome |
                      ConfigOption::McpSidecar;

    for (const ConfigOption &o: ConfigOptions::all())
    {
        EXPECT_NE(o.consumer(), 0) << o.key() << " is consumed by nobody";
        EXPECT_EQ(o.consumer() & ~known, 0) << o.key() << " has an unknown consumer bit";
    }
}

TEST(ConfigOptionsTable, FindLocatesEveryKeyAndOnlyThem)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        const ConfigOption *f = ConfigOptions::find(o.key());
        ASSERT_NE(f, nullptr) << o.key();
        EXPECT_EQ(f->key(), o.key());
    }

    EXPECT_EQ(ConfigOptions::find("no_such_option_at_all"), nullptr);
    EXPECT_EQ(ConfigOptions::find(""), nullptr);
}

TEST(ConfigOptionsTable, CategoriesCoverTheWholeTable)
{
    std::vector<C> cats = ConfigOptions::categories();

    std::set<int> uniq;
    for (C c: cats)
        EXPECT_TRUE(uniq.insert(static_cast<int>(c)).second) << "duplicate category";

    //Unknown is declared for the keys of the file that are not in the registry,
    //no declared option may use it.
    EXPECT_TRUE(uniq.count(static_cast<int>(C::Unknown)));

    std::size_t total = 0;
    for (C c: cats)
    {
        std::vector<const ConfigOption *> o = ConfigOptions::byCategory(c);
        total += o.size();
        if (c == C::Unknown)
        {
            EXPECT_TRUE(o.empty()) << "an option is declared in the Unknown category";
        }
    }

    EXPECT_EQ(total, ConfigOptions::all().size());
}

TEST(ConfigOptionsTable, CategoryAndTypeLabelsAreDefined)
{
    for (C c: ConfigOptions::categories())
        EXPECT_FALSE(ConfigOptions::categoryLabel(c).empty()) << "category " << static_cast<int>(c);

    for (T t: allTypes())
        EXPECT_FALSE(ConfigOptions::typeLabel(t).empty()) << "type " << static_cast<int>(t);

    //Every type of the registry is one of the values listed above
    std::set<int> known;
    for (T t: allTypes())
        known.insert(static_cast<int>(t));
    for (const ConfigOption &o: ConfigOptions::all())
        EXPECT_TRUE(known.count(static_cast<int>(o.type()))) << o.key();
}

TEST(ConfigOptionsTable, ConsumerLabels)
{
    EXPECT_FALSE(ConfigOptions::consumerLabel(ConfigOption::Server).empty());
    EXPECT_FALSE(ConfigOptions::consumerLabel(ConfigOption::CalaosHome).empty());
    EXPECT_FALSE(ConfigOptions::consumerLabel(ConfigOption::McpSidecar).empty());
    EXPECT_FALSE(ConfigOptions::consumerLabel(0).empty());
}

//-----------------------------------------------------------------------------
// Declarations consistency
//-----------------------------------------------------------------------------

TEST(ConfigOptionsTable, LiteralDefaultsAreValid)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (!o.hasDef())
            continue;

        std::string err;
        EXPECT_TRUE(o.validate(o.def(), &err))
            << o.key() << " has a default its own validate() refuses: "
            << o.def() << " (" << err << ")";

        if (o.pattern() && !o.def().empty())
        {
            std::regex re(o.pattern());
            EXPECT_TRUE(std::regex_match(o.def(), re))
                << o.key() << " default does not match its own pattern";
        }

        //A generated option cannot have a literal default, it is produced at
        //the first start.
        EXPECT_FALSE(o.isGenerated())
            << o.key() << " is generated() and yet declares a literal default";
    }
}

TEST(ConfigOptionsTable, RangesAreOrdered)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (!o.hasRange())
            continue;

        EXPECT_LT(o.rangeMin(), o.rangeMax()) << o.key() << " has an empty range";

        //A range only makes sense for a number
        EXPECT_TRUE(o.type() == T::Int || o.type() == T::Float || o.type() == T::Port)
            << o.key() << " declares a range but is not a number";

        if (o.hasDef() && !o.def().empty())
        {
            double d = 0.0;
            ASSERT_TRUE(Utils::from_string(o.def(), d)) << o.key();
            EXPECT_GE(d, o.rangeMin()) << o.key();
            EXPECT_LE(d, o.rangeMax()) << o.key();
        }
    }
}

TEST(ConfigOptionsTable, EnumsAreUsable)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (o.type() != T::Enum)
            continue;

        EXPECT_GE(o.values().size(), 2u) << o.key() << " is an Enum with less than 2 values";

        std::set<std::string> seen;
        for (const auto &kv: o.values())
        {
            EXPECT_FALSE(kv.first.empty()) << o.key() << " has an empty enum value";
            EXPECT_NE(kv.second, nullptr) << o.key() << " has an unlabelled enum value";
            EXPECT_TRUE(seen.insert(kv.first).second)
                << o.key() << " lists " << kv.first << " twice";
        }

        if (o.hasDef())
        {
            EXPECT_TRUE(seen.count(o.def()))
                << o.key() << " default " << o.def() << " is not one of its values";
        }
    }

    //Whatever the type, a declared value list must contain the default
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (o.values().empty() || !o.hasDef() || o.def().empty())
            continue;

        EXPECT_FALSE(o.valueLabel(o.def()).empty())
            << o.key() << " default " << o.def() << " is not in its value list";
    }
}

TEST(ConfigOptionsTable, PatternsCompileAndAreAnchored)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (!o.pattern())
            continue;

        std::string p = o.pattern();
        EXPECT_NO_THROW({ std::regex re(p); }) << o.key() << " has an invalid regex";
        EXPECT_EQ(p.front(), '^') << o.key() << " pattern is not anchored";
        EXPECT_EQ(p.back(), '$') << o.key() << " pattern is not anchored";
    }
}

TEST(ConfigOptionsTable, DeprecatedOptionsPointAtSomethingReal)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        if (!o.isDeprecated() || o.replacement().empty())
            continue;

        EXPECT_NE(ConfigOptions::find(o.replacement()), nullptr)
            << o.key() << " is replaced by an unknown key: " << o.replacement();
        EXPECT_NE(o.replacement(), o.key()) << o.key() << " replaces itself";
    }
}

TEST(ConfigOptionsTable, SeeAlsoIsSymmetricAndPointsAtRealKeys)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        for (const std::string &other: o.seeAlso())
        {
            EXPECT_NE(other, o.key()) << o.key() << " refers to itself";

            const ConfigOption *target = ConfigOptions::find(other);
            ASSERT_NE(target, nullptr)
                << o.key() << " refers to an unknown key: " << other;

            const std::vector<std::string> &back = target->seeAlso();
            EXPECT_NE(std::find(back.begin(), back.end(), o.key()), back.end())
                << other << " does not refer back to " << o.key();
        }
    }
}

//Options nobody but Calaos Home or the MCP sidecar reads must say so, otherwise
//the TUI would offer to delete a key something still depends on.
TEST(ConfigOptionsTable, KnownNonServerOptions)
{
    const char *homeOnly[] = { "show_cursor", "dpms_enable", "dpms_standby",
                               "calaos_server_host", "lang", "user_emails",
                               "calaos/host" };
    for (const char *k: homeOnly)
    {
        const ConfigOption &o = opt(k);
        EXPECT_EQ(o.consumer(), ConfigOption::CalaosHome)
            << k << " should be consumed by Calaos Home only";
    }

    const char *mcpOnly[] = { "mcp_rate_limit", "mcp_ban_failures", "mcp_ban_seconds" };
    for (const char *k: mcpOnly)
    {
        const ConfigOption &o = opt(k);
        EXPECT_EQ(o.consumer(), ConfigOption::McpSidecar)
            << k << " should be consumed by the MCP sidecar only";
    }

    EXPECT_EQ(opt("cn_user").consumer(),
              ConfigOption::Server | ConfigOption::CalaosHome);
    EXPECT_EQ(opt("cn_pass").consumer(),
              ConfigOption::Server | ConfigOption::CalaosHome);
    EXPECT_EQ(opt("latitude").consumer(),
              ConfigOption::Server | ConfigOption::CalaosHome);
    EXPECT_EQ(opt("longitude").consumer(),
              ConfigOption::Server | ConfigOption::CalaosHome);
}

//The values the code really uses. A wrong default here is a wrong "(default)"
//shown by the TUI and a wrong generated documentation.
TEST(ConfigOptionsTable, DefaultsMatchTheCode)
{
    EXPECT_EQ(opt("cn_user").def(), "user");
    EXPECT_EQ(opt("cn_pass").def(), "pass");
    EXPECT_EQ(opt("port_api").def(), "5454");
    EXPECT_EQ(opt("listen_address").def(), "0.0.0.0");
    EXPECT_EQ(opt("debug_level").def(), "4");
    EXPECT_EQ(opt("influxdb_version").def(), "1");
    EXPECT_EQ(opt("influxdb_host").def(), "127.0.0.1");
    EXPECT_EQ(opt("influxdb_port").def(), "8086");
    EXPECT_EQ(opt("influxdb_log_timeout").def(), "300");
    EXPECT_EQ(opt("influxdb_database").def(), "calaos");
    EXPECT_EQ(opt("influxdb_org").def(), "calaos");
    EXPECT_EQ(opt("influxdb_bucket").def(), "calaos-data");
    EXPECT_EQ(opt("history_keep_days").def(), "30");
    EXPECT_EQ(opt("notif/mail_sender").def(), "calaos@localhost");
    EXPECT_EQ(opt("mcp_rate_limit").def(), "300");
    EXPECT_EQ(opt("mcp_ban_failures").def(), "20");
    EXPECT_EQ(opt("mcp_ban_seconds").def(), "120");
    EXPECT_EQ(opt("ota_enabled").def(), "true");
    EXPECT_EQ(opt("ota_rescan_interval").def(), "60");

    //TimeRange.cpp falls back to the centre of France when the key is absent,
    //which is what the registry declares; initConfigOptions seeds Paris.
    EXPECT_EQ(opt("latitude").def(), "46.422713");
    EXPECT_EQ(opt("longitude").def(), "2.548828");

    //Auto generated at the first start, no literal default
    EXPECT_FALSE(opt("mcp_token").hasDef());
    EXPECT_TRUE(opt("mcp_token").isGenerated());
    EXPECT_FALSE(opt("mcp_service_token").hasDef());
    EXPECT_TRUE(opt("mcp_service_token").isGenerated());

    //Computed from the installation prefix
    EXPECT_FALSE(opt("wwwroot").defDynamic().empty());
    EXPECT_FALSE(opt("debug_wwwroot").defDynamic().empty());
    EXPECT_FALSE(opt("ota_firmware_path").defDynamic().empty());

    //Secrets
    EXPECT_TRUE(opt("cn_pass").isSecret());
    EXPECT_TRUE(opt("calaos_password").isSecret());
    EXPECT_TRUE(opt("smtp_password").isSecret());
    EXPECT_TRUE(opt("influxdb_token").isSecret());
    EXPECT_TRUE(opt("mcp_token").isSecret());
    EXPECT_TRUE(opt("mcp_service_token").isSecret());
}

//-----------------------------------------------------------------------------
// validate()
//-----------------------------------------------------------------------------

TEST(ConfigOptionsValidate, EmptyIsAlwaysAccepted)
{
    for (const ConfigOption &o: ConfigOptions::all())
    {
        std::string err;
        EXPECT_TRUE(o.validate("", &err)) << o.key() << ": " << err;
        EXPECT_TRUE(err.empty()) << o.key();
    }
}

TEST(ConfigOptionsValidate, ErrorMessageIsFilledOnFailure)
{
    std::string err;
    EXPECT_FALSE(opt("port_api").validate("abc", &err));
    EXPECT_FALSE(err.empty());

    //The error pointer is optional
    EXPECT_FALSE(opt("port_api").validate("abc", nullptr));
    EXPECT_FALSE(opt("port_api").validate("abc"));
}

TEST(ConfigOptionsValidate, Port)
{
    const ConfigOption &o = opt("port_api");

    EXPECT_TRUE(o.validate("5454"));
    EXPECT_TRUE(o.validate("1"));
    EXPECT_TRUE(o.validate("65535"));

    EXPECT_FALSE(o.validate("0"));
    EXPECT_FALSE(o.validate("65536"));
    EXPECT_FALSE(o.validate("-1"));
    EXPECT_FALSE(o.validate("abc"));
    EXPECT_FALSE(o.validate("54 54"));
    EXPECT_FALSE(o.validate("5454.0"));
}

TEST(ConfigOptionsValidate, Bool)
{
    const ConfigOption &o = opt("notif/battery_mail_enabled");

    EXPECT_TRUE(o.validate("true"));
    EXPECT_TRUE(o.validate("false"));

    //The reading code compares to "true" verbatim, nothing else means true
    EXPECT_FALSE(o.validate("1"));
    EXPECT_FALSE(o.validate("0"));
    EXPECT_FALSE(o.validate("yes"));
    EXPECT_FALSE(o.validate("True"));
}

//ota_enabled is the one boolean whose reading code accepts "1" as well
TEST(ConfigOptionsValidate, BoolAcceptingOne)
{
    const ConfigOption &o = opt("ota_enabled");

    EXPECT_TRUE(o.validate("true"));
    EXPECT_TRUE(o.validate("false"));
    EXPECT_TRUE(o.validate("1"));
    EXPECT_TRUE(o.validate("0"));

    EXPECT_FALSE(o.validate("2"));
    EXPECT_FALSE(o.validate("yes"));
}

TEST(ConfigOptionsValidate, Int)
{
    const ConfigOption &o = opt("history_keep_days");

    EXPECT_TRUE(o.validate("30"));
    EXPECT_TRUE(o.validate("1"));
    EXPECT_TRUE(o.validate("3650"));

    EXPECT_FALSE(o.validate("0"));
    EXPECT_FALSE(o.validate("4000"));
    EXPECT_FALSE(o.validate("abc"));
    EXPECT_FALSE(o.validate("30.5"));
    EXPECT_FALSE(o.validate("30 days"));
}

TEST(ConfigOptionsValidate, Float)
{
    const ConfigOption &o = opt("latitude");

    EXPECT_TRUE(o.validate("48.864715"));
    EXPECT_TRUE(o.validate("-90"));
    EXPECT_TRUE(o.validate("90"));
    EXPECT_TRUE(o.validate("0.0"));

    //The reading code parses with the C locale, a comma truncates the value
    EXPECT_FALSE(o.validate("48,864715"));
    EXPECT_FALSE(o.validate("91"));
    EXPECT_FALSE(o.validate("-90.5"));
    EXPECT_FALSE(o.validate("north"));
}

TEST(ConfigOptionsValidate, Host)
{
    const ConfigOption &o = opt("listen_address");

    EXPECT_TRUE(o.validate("0.0.0.0"));
    EXPECT_TRUE(o.validate("192.168.1.10"));
    EXPECT_TRUE(o.validate("::1"));
    EXPECT_TRUE(o.validate("calaos.local"));

    EXPECT_FALSE(o.validate("http://calaos.local"));
    EXPECT_FALSE(o.validate("calaos local"));
    EXPECT_FALSE(o.validate("calaos/local"));
}

TEST(ConfigOptionsValidate, Email)
{
    const ConfigOption &o = opt("notif/mail_sender");

    EXPECT_TRUE(o.validate("calaos@localhost"));
    EXPECT_TRUE(o.validate("alice@example.org"));

    EXPECT_FALSE(o.validate("alice"));
    EXPECT_FALSE(o.validate("@example.org"));
    EXPECT_FALSE(o.validate("alice@"));
    EXPECT_FALSE(o.validate("alice@example.org, bob@example.org"));
}

TEST(ConfigOptionsValidate, EmailList)
{
    const ConfigOption &o = opt("notif/mail_recipients");

    EXPECT_TRUE(o.validate("alice@example.org"));
    EXPECT_TRUE(o.validate("alice@example.org,bob@example.org"));
    EXPECT_TRUE(o.validate("alice@example.org, bob@example.org"));

    EXPECT_FALSE(o.validate("alice@example.org,bob"));
    EXPECT_FALSE(o.validate("alice"));

    //user_emails is the Calaos Home counterpart, same rules
    EXPECT_TRUE(opt("user_emails").validate("alice@example.org,bob@example.org"));
    EXPECT_FALSE(opt("user_emails").validate("nobody"));
}

TEST(ConfigOptionsValidate, Enum)
{
    const ConfigOption &o = opt("influxdb_version");

    EXPECT_TRUE(o.validate("1"));
    EXPECT_TRUE(o.validate("2"));

    EXPECT_FALSE(o.validate("3"));
    EXPECT_FALSE(o.validate("1.8"));
    EXPECT_FALSE(o.validate("v2"));

    EXPECT_TRUE(opt("lang").validate("fr"));
    EXPECT_FALSE(opt("lang").validate("klingon"));
}

TEST(ConfigOptionsValidate, CsvWithAPattern)
{
    const ConfigOption &o = opt("debug_domains");

    EXPECT_TRUE(o.validate("hifirose:5,network:0"));
    EXPECT_TRUE(o.validate("network:3"));

    EXPECT_FALSE(o.validate("hifirose"));
    EXPECT_FALSE(o.validate("hifirose:5,"));
    EXPECT_FALSE(o.validate("hifirose:9"));
}

TEST(ConfigOptionsValidate, TokenPattern)
{
    const ConfigOption &o = opt("mcp_token");

    EXPECT_TRUE(o.validate(std::string(64, 'a')));
    EXPECT_TRUE(o.validate("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef"));

    EXPECT_FALSE(o.validate("too-short"));
    EXPECT_FALSE(o.validate(std::string(63, 'a')));
    EXPECT_FALSE(o.validate(std::string(64, 'z')));

    //An InfluxDB token is a free form string, no pattern there
    EXPECT_TRUE(opt("influxdb_token").validate("Zm9vYmFyLXRva2Vu=="));
}

TEST(ConfigOptionsValidate, FreeFormTypesAcceptAnything)
{
    EXPECT_TRUE(opt("smtp_username").validate("bob@example.org"));
    EXPECT_TRUE(opt("smtp_password").validate("p@ssw0rd !#$"));
    EXPECT_TRUE(opt("wwwroot").validate("/srv/calaos/app"));
    EXPECT_TRUE(opt("wwwroot").validate("relative/path"));

    //A line break would corrupt the XML attribute, refuse it everywhere
    EXPECT_FALSE(opt("smtp_username").validate("bob\nmallory"));
    EXPECT_FALSE(opt("wwwroot").validate("/srv\r/app"));
}

TEST(ConfigOptionsValidate, DisplayValue)
{
    EXPECT_EQ(opt("cn_user").displayValue(""), "");
    EXPECT_EQ(opt("cn_user").displayValue("bob"), "bob");

    //Secrets never leak through displayValue
    EXPECT_EQ(opt("cn_pass").displayValue(""), "");
    EXPECT_NE(opt("cn_pass").displayValue("hunter2"), "hunter2");

    EXPECT_FALSE(opt("notif/battery_mail_enabled").displayValue("true").empty());
    EXPECT_NE(opt("notif/battery_mail_enabled").displayValue("true"),
              opt("notif/battery_mail_enabled").displayValue("false"));

    //An enum shows its human label, not the raw key
    EXPECT_NE(opt("influxdb_version").displayValue("2"), "2");
}

//-----------------------------------------------------------------------------
// suggest()
//-----------------------------------------------------------------------------

TEST(ConfigOptionsSuggest, FindsTypos)
{
    EXPECT_EQ(ConfigOptions::suggest("port_apx"), "port_api");
    EXPECT_EQ(ConfigOptions::suggest("cn_pas"), "cn_pass");
    EXPECT_EQ(ConfigOptions::suggest("influxdb_hosts"), "influxdb_host");
    EXPECT_EQ(ConfigOptions::suggest("smtp_serveur"), "smtp_server");
}

TEST(ConfigOptionsSuggest, GivesUpOnNonsense)
{
    EXPECT_EQ(ConfigOptions::suggest(""), "");
    EXPECT_EQ(ConfigOptions::suggest("xyzzy"), "");
    EXPECT_EQ(ConfigOptions::suggest("this_is_not_a_config_key_at_all"), "");
}

TEST(ConfigOptionsSuggest, AnExactKeyIsItsOwnSuggestion)
{
    for (const ConfigOption &o: ConfigOptions::all())
        EXPECT_EQ(ConfigOptions::suggest(o.key()), o.key());
}

//-----------------------------------------------------------------------------
// Obsolete keys
//-----------------------------------------------------------------------------

TEST(ConfigOptionsObsolete, TheFiveDeadKeys)
{
    const std::vector<std::string> &keys = ConfigOptions::obsoleteKeys();
    ASSERT_EQ(keys.size(), 5u);

    const char *expected[] = { "hwid", "use_ntp", "fw_target", "fw_version", "device_type" };
    for (const char *k: expected)
    {
        EXPECT_NE(std::find(keys.begin(), keys.end(), std::string(k)), keys.end())
            << k << " is missing from obsoleteKeys()";
        EXPECT_TRUE(ConfigOptions::isObsolete(k)) << k;
    }

    EXPECT_FALSE(ConfigOptions::isObsolete("port_api"));
    EXPECT_FALSE(ConfigOptions::isObsolete(""));

    //calaos/host is known pollution but is deliberately never purged
    EXPECT_FALSE(ConfigOptions::isObsolete("calaos/host"));
    EXPECT_NE(ConfigOptions::find("calaos/host"), nullptr);
}

TEST(ConfigOptionsObsolete, DisjointFromTheRegistry)
{
    for (const ConfigOption &o: ConfigOptions::all())
        EXPECT_FALSE(ConfigOptions::isObsolete(o.key()))
            << o.key() << " is both declared and obsolete";
}

//-----------------------------------------------------------------------------
// Generated documentation
//-----------------------------------------------------------------------------

TEST(ConfigOptionsDoc, JsonParsesAndListsEverything)
{
    std::string raw = ConfigOptions::genJson();
    ASSERT_FALSE(raw.empty());

    nlohmann::json j;
    ASSERT_NO_THROW(j = nlohmann::json::parse(raw));

    ASSERT_TRUE(j.contains("options"));
    ASSERT_TRUE(j["options"].is_array());
    EXPECT_EQ(j["options"].size(), ConfigOptions::all().size());

    std::set<std::string> keys;
    for (const auto &o: j["options"])
    {
        ASSERT_TRUE(o.contains("key"));
        ASSERT_TRUE(o.contains("label"));
        ASSERT_TRUE(o.contains("doc"));
        ASSERT_TRUE(o.contains("type"));
        ASSERT_TRUE(o.contains("category"));
        ASSERT_TRUE(o.contains("consumers"));
        EXPECT_FALSE(o["consumers"].empty()) << o["key"];
        keys.insert(o["key"].get<std::string>());
    }

    for (const ConfigOption &o: ConfigOptions::all())
        EXPECT_TRUE(keys.count(o.key())) << o.key() << " is missing from genJson()";

    ASSERT_TRUE(j.contains("obsolete_keys"));
    EXPECT_EQ(j["obsolete_keys"].size(), ConfigOptions::obsoleteKeys().size());

    ASSERT_TRUE(j.contains("categories"));
    EXPECT_EQ(j["categories"].size(), ConfigOptions::categories().size());
}

TEST(ConfigOptionsDoc, MarkdownMentionsEveryKey)
{
    std::string md = ConfigOptions::genMarkdown();
    ASSERT_FALSE(md.empty());

    for (const ConfigOption &o: ConfigOptions::all())
    {
        EXPECT_NE(md.find("`" + o.key() + "`"), std::string::npos)
            << o.key() << " is missing from genMarkdown()";
    }

    for (const std::string &k: ConfigOptions::obsoleteKeys())
    {
        EXPECT_NE(md.find("`" + k + "`"), std::string::npos)
            << k << " is missing from the obsolete section of genMarkdown()";
    }

    //One section per non empty category
    for (C c: ConfigOptions::categories())
    {
        if (ConfigOptions::byCategory(c).empty())
            continue;
        EXPECT_NE(md.find("## " + ConfigOptions::categoryLabel(c)), std::string::npos)
            << "missing section for category " << static_cast<int>(c);
    }
}

//-----------------------------------------------------------------------------
// purgeObsolete()
//-----------------------------------------------------------------------------

/* Runs on a private config directory. initConfigOptions() is the only entry
 * point that resets the config path cached by the library, so it is called once
 * per test and the config file is then written by hand.
 */
class ConfigOptionsPurgeTest: public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        char tmpl[] = "/tmp/calaos_cfgopt_XXXXXX";
        char *d = mkdtemp(tmpl);
        ASSERT_NE(d, nullptr);
        base = d;

        confDir = base + "/config";
        cacheDir = base + "/cache";
        ASSERT_EQ(::mkdir(confDir.c_str(), 0755), 0);
        ASSERT_EQ(::mkdir(cacheDir.c_str(), 0755), 0);

        std::vector<char> c(confDir.begin(), confDir.end());
        c.push_back('\0');
        std::vector<char> k(cacheDir.begin(), cacheDir.end());
        k.push_back('\0');
        Utils::initConfigOptions(c.data(), k.data(), true);
    }

    virtual void TearDown()
    {
        std::string cmd = "rm -rf '" + base + "'";
        if (::system(cmd.c_str()) != 0)
            std::cerr << "warning: could not clean " << base << std::endl;
    }

    std::string configFile() const { return confDir + "/local_config.xml"; }

    static void writeFile(const std::string &path, const std::string &content)
    {
        std::ofstream f(path.c_str(), std::ofstream::out | std::ofstream::trunc);
        f << content;
        f.close();
    }

    static std::string readFile(const std::string &path)
    {
        std::ifstream f(path.c_str());
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    //Write local_config.xml the way another program would
    void writeOptions(const std::vector<std::pair<std::string, std::string>> &opts)
    {
        std::string body;
        for (const auto &kv: opts)
            body += "  <calaos:option name=\"" + kv.first + "\" value=\"" + kv.second + "\" />\n";

        writeFile(configFile(),
                  "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
                  "<calaos:config xmlns:calaos=\"http://www.calaos.fr\">\n" +
                  body +
                  "</calaos:config>\n");
    }

    static std::map<std::string, std::string> currentOptions()
    {
        Params p;
        std::map<std::string, std::string> res;
        if (!Utils::get_config_options(p))
            return res;

        for (int i = 0; i < p.size(); i++)
        {
            std::string k, v;
            p.get_item(i, k, v);
            res[k] = v;
        }
        return res;
    }

    std::string base;
    std::string confDir;
    std::string cacheDir;
};

TEST_F(ConfigOptionsPurgeTest, NothingToPurgeIsAByteIdenticalNoOp)
{
    writeOptions({ { "cn_user", "user" },
                   { "cn_pass", "pass" },
                   { "port_api", "5454" } });

    std::string before = readFile(configFile());

    EXPECT_TRUE(ConfigOptions::obsoletePresent().empty());

    std::vector<std::string> removed = { "sentinel" };
    EXPECT_TRUE(ConfigOptions::purgeObsolete(&removed));
    EXPECT_TRUE(removed.empty());

    //Not a single byte written: the file is not even rewritten
    EXPECT_EQ(readFile(configFile()), before);

    //And it works without the out parameter too
    EXPECT_TRUE(ConfigOptions::purgeObsolete());
    EXPECT_EQ(readFile(configFile()), before);
}

TEST_F(ConfigOptionsPurgeTest, RemovesOnlyTheObsoleteKeys)
{
    writeOptions({ { "cn_user", "user" },
                   { "hwid", "0011223344" },
                   { "cn_pass", "s3cret" },
                   { "use_ntp", "true" },
                   { "port_api", "5454" },
                   { "fw_target", "calaos_tss" },
                   { "smtp_server", "smtp.example.org" },
                   { "fw_version", "3.0" },
                   { "notif/mail_recipients", "alice@example.org" },
                   { "device_type", "calaos_server" },
                   //A key nobody knows about must survive untouched
                   { "some_unknown_legacy_key", "keep me" } });

    std::map<std::string, std::string> before = currentOptions();
    ASSERT_EQ(before.size(), 11u);

    std::vector<std::string> present = ConfigOptions::obsoletePresent();
    EXPECT_EQ(present.size(), 5u);

    std::vector<std::string> removed;
    ASSERT_TRUE(ConfigOptions::purgeObsolete(&removed));

    std::set<std::string> gone(removed.begin(), removed.end());
    EXPECT_EQ(gone.size(), 5u);
    for (const std::string &k: ConfigOptions::obsoleteKeys())
        EXPECT_TRUE(gone.count(k)) << k << " was not reported as removed";

    std::map<std::string, std::string> after = currentOptions();

    //Exactly the five obsolete keys are gone, every other key keeps its value
    for (const auto &kv: before)
    {
        if (ConfigOptions::isObsolete(kv.first))
        {
            EXPECT_EQ(after.count(kv.first), 0u) << kv.first << " survived the purge";
            continue;
        }
        ASSERT_EQ(after.count(kv.first), 1u) << kv.first << " was wrongly removed";
        EXPECT_EQ(after[kv.first], kv.second) << kv.first << " was modified";
    }
    EXPECT_EQ(after.size(), before.size() - 5);

    //No obsolete key remains in the file itself
    std::string raw = readFile(configFile());
    for (const std::string &k: ConfigOptions::obsoleteKeys())
        EXPECT_EQ(raw.find("\"" + k + "\""), std::string::npos) << k;

    //And a second run is a no-op
    std::string after1 = readFile(configFile());
    removed.clear();
    EXPECT_TRUE(ConfigOptions::purgeObsolete(&removed));
    EXPECT_TRUE(removed.empty());
    EXPECT_EQ(readFile(configFile()), after1);
}

TEST_F(ConfigOptionsPurgeTest, PurgesASubsetOfTheDeadKeys)
{
    writeOptions({ { "port_api", "5454" },
                   { "use_ntp", "true" } });

    std::vector<std::string> present = ConfigOptions::obsoletePresent();
    ASSERT_EQ(present.size(), 1u);
    EXPECT_EQ(present[0], "use_ntp");

    std::vector<std::string> removed;
    ASSERT_TRUE(ConfigOptions::purgeObsolete(&removed));
    ASSERT_EQ(removed.size(), 1u);
    EXPECT_EQ(removed[0], "use_ntp");

    std::map<std::string, std::string> after = currentOptions();
    EXPECT_EQ(after.size(), 1u);
    EXPECT_EQ(after["port_api"], "5454");
}
