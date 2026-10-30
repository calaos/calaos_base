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
#include "AdminConfig.h"
#include "CalaosConfig.h"
#include "ConfigOptions.h"
#include "JsonField.h"
#include "version.h"

#include <unistd.h>

using namespace Calaos;

namespace
{

//Far above any key of the table; a longer value is a client error, not a setting
constexpr size_t MaxValueBytes = 4096;

//The value a reading site falls back to when the key is unset
string defaultOf(const ConfigOption &opt)
{
    return opt.hasDef() ? opt.def() : string();
}

//config_set's echo: a secret only tells whether it is stored
Json setResult(const AdminConfig::Entry &e, const ConfigOption &opt)
{
    const string stored = Utils::get_config_option(e.key);
    Json j = { { "key", e.key }, { "set", !stored.empty() }, { "restart_required", e.restart } };
    if (!opt.isSecret())
        j["value"] = stored.empty() ? defaultOf(opt) : stored;
    return j;
}

Json describe(const AdminConfig::Entry &e, const ConfigOption &opt)
{
    const string stored = Utils::get_config_option(e.key);
    Json j = Json::object();
    j["key"] = e.key;
    j["group"] = e.group;
    j["type"] = ConfigOptions::typeId(opt.type());
    j["description"] = opt.label();
    j["default"] = defaultOf(opt);
    j["secret"] = opt.isSecret();
    j["set"] = !stored.empty();
    j["restart"] = e.restart;
    if (!opt.isSecret())
        j["value"] = stored.empty() ? defaultOf(opt) : stored;

    //The raw values only: their labels are translated for the server's locale
    if (!opt.values().empty())
    {
        Json choices = Json::array();
        for (const auto &kv: opt.values())
            choices.push_back(kv.first);
        j["choices"] = choices;
    }
    return j;
}

string utcStamp(std::time_t t, const char *format)
{
    std::tm tm = {};
    gmtime_r(&t, &tm);
    char buf[32];
    return strftime(buf, sizeof(buf), format, &tm) ? string(buf) : string();
}

} //namespace

const std::vector<AdminConfig::Entry> &AdminConfig::entries()
{
    static const std::vector<Entry> table = {
        { "debug_level", "Logging", true },
        { "debug_domains", "Logging", true },
        { "smtp_server", "Notifications", false },
        { "smtp_port", "Notifications", false },
        { "smtp_auth", "Notifications", false },
        { "smtp_tls", "Notifications", false },
        { "smtp_username", "Notifications", false },
        { "smtp_password", "Notifications", false },
        { "user_emails", "Notifications", false },
        { "history_keep_days", "History", false },
        { "influxdb_enabled", "InfluxDB", true },
        { "influxdb_version", "InfluxDB", true },
        { "influxdb_host", "InfluxDB", true },
        { "influxdb_port", "InfluxDB", true },
        { "influxdb_database", "InfluxDB", true },
        { "influxdb_org", "InfluxDB", true },
        { "influxdb_bucket", "InfluxDB", true },
        { "influxdb_token", "InfluxDB", true },
        { "influxdb_log_timeout", "InfluxDB", true },
        { "mcp_rate_limit", "MCP", true },
        { "mcp_ban_failures", "MCP", true },
        { "mcp_ban_seconds", "MCP", true },
        { "ota_enabled", "OTA", true },
        { "ota_rescan_interval", "OTA", true },
        //OriginPolicy::allowedHosts() reads the option at every check
        { "api_allowed_hosts", "Origin", false },
        { "latitude", "Location", false },
        { "longitude", "Location", false },
        //Read by calaos-home at its next start, not by this server
        { "show_cursor", "Kiosk", false },
        { "dpms_enable", "Kiosk", false },
        { "dpms_standby", "Kiosk", false },
        { "lang", "Kiosk", false },
    };
    return table;
}

const AdminConfig::Entry *AdminConfig::find(const string &key)
{
    for (const Entry &e: entries())
    {
        if (key == e.key)
            return &e;
    }
    return nullptr;
}

bool AdminConfig::isAction(const string &action)
{
    return action == "config_list" || action == "config_set" || action == "config_backup";
}

Json AdminConfig::list()
{
    Json options = Json::array();
    for (const Entry &e: entries())
    {
        const ConfigOption *opt = ConfigOptions::find(e.key);
        if (opt)
            options.push_back(describe(e, *opt));
    }
    return Json{ { "options", options } };
}

string AdminConfig::setRefusal(const Json &params)
{
    const auto key = params.find("key");
    if (key == params.end() || !key->is_string())
        return "key is required";
    const auto value = params.find("value");
    if (value == params.end() || !value->is_string())
        return "value is required";

    const string k = key->get<string>();
    const ConfigOption *opt = ConfigOptions::find(k);
    if (!find(k) || !opt)
        return "This key cannot be set here";

    const string v = value->get<string>();
    if (v.size() > MaxValueBytes)
        return "value is longer than 4096 bytes";
    //A NUL would cut the XML attribute, a control character has no place in an option
    if (std::any_of(v.begin(), v.end(), [](char c)
                    {
        return (unsigned char)c < 0x20 || c == 0x7f;
        }))
        return "value must not contain a control character";

    string error;
    if (!opt->validate(v, &error))
        return error;
    return string();
}

Json AdminConfig::set(const Json &params)
{
    const string key = JsonField::string(params, "key");
    const string value = JsonField::string(params, "value");
    const Entry *e = find(key);
    const ConfigOption *opt = ConfigOptions::find(key);
    if (!e || !opt)
        return Json(); //LCOV_EXCL_LINE setRefusal() refused it before

    Config::Instance().BackupFiles();

    //An unset key makes every reading site fall back to its own default
    const bool written = value.empty() ? Utils::del_config_option(key) : Utils::set_config_option(key, value);
    if (!written)
        return Json(); //LCOV_EXCL_LINE local_config.xml unwritable (CI runs as root)
    return setResult(*e, *opt);
}

Json AdminConfig::backup(std::time_t createdAt)
{
    //The in-memory IOs and rules as config get hands them out
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();

    //widgets.xml has no reader; a missing file is left out so a restore never writes an empty one
    Json files = Json::object();
    for (const char *name: { IO_CONFIG, RULES_CONFIG, LOCAL_CONFIG })
    {
        const string path = Utils::getConfigFile(name);
        if (FileUtils::exists(path))
            files[name] = Utils::getFileContent(path.c_str());
    }

    return Json{ { "format", "calaos-config-backup/2" },
                 { "created_at", utcStamp(createdAt, "%Y-%m-%dT%H:%M:%SZ") },
                 { "server_version", PKG_VERSION_STR },
                 { "hostname", hostname() },
                 { "io_format", IO_FORMAT_DEVICES },
                 { "files", files } };
}

string AdminConfig::backupFilename(const string &hostname, std::time_t createdAt)
{
    //A hostname may carry a quote or a path separator: the header must not
    string host;
    for (char c: hostname)
    {
        if (host.size() == 32)
            break;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
            c == '-')
            host.push_back(c);
    }
    if (host.empty())
        host = "box";
    return "calaos-backup-" + host + "-" + utcStamp(createdAt, "%Y%m%d-%H%M") + ".json";
}

string AdminConfig::hostname()
{
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0)
        return string(); //LCOV_EXCL_LINE gethostname() only fails on a bad buffer
    return string(buf);
}
