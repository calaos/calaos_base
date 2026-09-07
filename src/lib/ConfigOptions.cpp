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
#include "ConfigOptions.h"
#include "Utils.h"
#include "FileUtils.h"

#include <cctype>
#include <regex>

using namespace Calaos;

//-----------------------------------------------------------------------------
// ConfigOption
//-----------------------------------------------------------------------------

ConfigOption::ConfigOption(std::string k, Category cat, Type t):
    m_key(std::move(k)),
    m_category(cat),
    m_type(t)
{
}

ConfigOption &ConfigOption::label(const char *l)
{
    m_label = l;
    return *this;
}

ConfigOption &ConfigOption::doc(const char *d)
{
    m_doc = d;
    return *this;
}

ConfigOption &ConfigOption::def(const std::string &v)
{
    m_def = v;
    m_hasDef = true;
    return *this;
}

ConfigOption &ConfigOption::defDynamic(const char *human)
{
    m_defDynamic = human;
    return *this;
}

ConfigOption &ConfigOption::range(double minValue, double maxValue)
{
    m_min = minValue;
    m_max = maxValue;
    m_hasRange = true;
    return *this;
}

ConfigOption &ConfigOption::values(ValueList kv)
{
    m_values = std::move(kv);
    return *this;
}

ConfigOption &ConfigOption::pattern(const char *regex)
{
    m_pattern = regex;
    return *this;
}

ConfigOption &ConfigOption::example(const char *e)
{
    m_example = e;
    return *this;
}

ConfigOption &ConfigOption::consumer(int mask)
{
    m_consumer = mask;
    return *this;
}

ConfigOption &ConfigOption::restartRequired()
{
    m_restartRequired = true;
    return *this;
}

ConfigOption &ConfigOption::generated()
{
    m_generated = true;
    return *this;
}

ConfigOption &ConfigOption::secret()
{
    m_secret = true;
    return *this;
}

ConfigOption &ConfigOption::confirmReset()
{
    m_confirmReset = true;
    return *this;
}

ConfigOption &ConfigOption::deprecated(const char *replacementKey)
{
    m_deprecated = true;
    if (replacementKey)
        m_replacement = replacementKey;
    return *this;
}

ConfigOption &ConfigOption::advanced()
{
    m_advanced = true;
    return *this;
}

ConfigOption &ConfigOption::seeAlso(const char *otherKey)
{
    if (otherKey)
        m_seeAlso.push_back(otherKey);
    return *this;
}

std::string ConfigOption::label() const
{
    return m_label ? std::string(_(m_label)) : std::string();
}

std::string ConfigOption::doc() const
{
    return m_doc ? std::string(_(m_doc)) : std::string();
}

std::string ConfigOption::defDynamic() const
{
    return m_defDynamic ? std::string(_(m_defDynamic)) : std::string();
}

std::string ConfigOption::valueLabel(const std::string &value) const
{
    for (const auto &kv: m_values)
    {
        if (kv.first == value)
            return kv.second ? std::string(_(kv.second)) : std::string();
    }
    return std::string();
}

//-----------------------------------------------------------------------------
// Validation helpers
//-----------------------------------------------------------------------------

namespace
{

//Human readable bound, without the trailing zeros an ostream would print
std::string formatNumber(double v)
{
    std::ostringstream oss;
    oss.imbue(std::locale("C"));
    if (v == static_cast<double>(static_cast<long long>(v)))
        oss << static_cast<long long>(v);
    else
        oss << std::setprecision(10) << v;
    return oss.str();
}

bool isIntegerLiteral(const std::string &v)
{
    if (v.empty())
        return false;

    std::size_t i = 0;
    if (v[0] == '+' || v[0] == '-')
        i = 1;
    if (i >= v.size())
        return false;

    for (; i < v.size(); i++)
    {
        if (!std::isdigit(static_cast<unsigned char>(v[i])))
            return false;
    }
    return true;
}

//Decimal number, C locale only: the reading code parses with std::locale("C"),
//so a comma would silently truncate the value.
bool isDecimalLiteral(const std::string &v)
{
    static const std::regex re("^[+-]?(([0-9]+(\\.[0-9]*)?)|(\\.[0-9]+))([eE][+-]?[0-9]+)?$");
    return std::regex_match(v, re);
}

bool isHostLiteral(const std::string &v)
{
    for (char c: v)
    {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            continue;
        //IPv6 literals, zone identifiers and the usual host name characters
        if (c == '.' || c == '-' || c == '_' || c == ':' ||
            c == '[' || c == ']' || c == '%')
            continue;
        return false;
    }
    return true;
}

bool isEmailLiteral(const std::string &v)
{
    std::size_t at = v.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= v.size())
        return false;
    if (v.find('@', at + 1) != std::string::npos)
        return false;

    for (char c: v)
    {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u) || c == ',' || c == ';' || c == '<' || c == '>')
            return false;
    }
    return true;
}

double toDouble(const std::string &v)
{
    double d = 0.0;
    std::istringstream iss(v);
    iss.imbue(std::locale("C"));
    iss >> d;
    return d;
}

} //namespace

bool ConfigOption::validate(const std::string &value, std::string *error) const
{
    if (error)
        error->clear();

    auto fail = [error](const std::string &msg) -> bool
    {
        if (error)
            *error = msg;
        return false;
    };

    /* An unset key is always valid: every reading site of calaos_base falls
     * back to its own default when the option is missing or empty.
     */
    if (value.empty())
        return true;

    if (value.find('\n') != std::string::npos ||
        value.find('\r') != std::string::npos)
        return fail(_("value must not contain a line break"));

    if (m_pattern)
    {
        try
        {
            std::regex re(m_pattern);
            if (!std::regex_match(value, re))
            {
                std::string msg = _("value does not match the expected format");
                if (m_example)
                    msg += std::string(", ") + _("for instance") + " " + m_example;
                return fail(msg);
            }
        }
        catch (const std::regex_error &)
        {
            //A broken pattern in the table must not make a value unusable
            cErrorDom("config") << "Invalid regex for option " << m_key << ": " << m_pattern;
        }
    }

    //A declared list of values is authoritative, whatever the type
    if (!m_values.empty())
    {
        std::string accepted;
        for (const auto &kv: m_values)
        {
            if (kv.first == value)
                return true;
            if (!accepted.empty())
                accepted += ", ";
            accepted += kv.first;
        }
        return fail(std::string(_("value must be one of:")) + " " + accepted);
    }

    switch (m_type)
    {
    case Type::Bool:
        if (value != "true" && value != "false")
            return fail(_("value must be true or false"));
        break;

    case Type::Int:
        if (!isIntegerLiteral(value))
            return fail(_("value must be a whole number"));
        break;

    case Type::Float:
        if (!isDecimalLiteral(value))
            return fail(_("value must be a decimal number, with a dot as the decimal separator"));
        break;

    case Type::Port:
        if (!isIntegerLiteral(value))
            return fail(_("value must be a whole number"));
        if (toDouble(value) < 1 || toDouble(value) > 65535)
            return fail(_("a TCP port must be between 1 and 65535"));
        break;

    case Type::Host:
        if (!isHostLiteral(value))
            return fail(_("value must be a host name or an IP address, without a scheme or a path"));
        break;

    case Type::Email:
        if (!isEmailLiteral(value))
            return fail(_("value must be an e-mail address, of the form name@example.org"));
        break;

    case Type::EmailList:
    {
        std::vector<std::string> parts;
        Utils::split(value, parts, ",");
        for (const auto &p: parts)
        {
            std::string mail = Utils::trim(p);
            if (mail.empty())
                return fail(_("the list must not contain an empty entry"));
            if (!isEmailLiteral(mail))
                return fail(std::string(_("not an e-mail address:")) + " " + mail);
        }
        break;
    }

    case Type::Csv:
    {
        std::vector<std::string> parts;
        Utils::split(value, parts, ",");
        for (const auto &p: parts)
        {
            if (Utils::trim(p).empty())
                return fail(_("the list must not contain an empty entry"));
        }
        break;
    }

    case Type::Enum:
        //An Enum without a declared list accepts anything, that would be a
        //table mistake and the unit tests refuse it.
        break;

    case Type::String:
    case Type::Path:
    case Type::Password:
    case Type::Token:
        break;
    }

    if (m_hasRange &&
        (m_type == Type::Int || m_type == Type::Float || m_type == Type::Port))
    {
        double d = toDouble(value);
        if (d < m_min || d > m_max)
        {
            return fail(std::string(_("value must be between")) + " " +
                        formatNumber(m_min) + " " + _("and") + " " + formatNumber(m_max));
        }
    }

    return true;
}

std::string ConfigOption::displayValue(const std::string &raw) const
{
    if (raw.empty())
        return std::string();

    if (m_secret)
        return "••••••••";

    std::string lbl = valueLabel(raw);
    if (!lbl.empty())
        return lbl;

    if (m_type == Type::Bool)
    {
        if (raw == "true" || raw == "1")
            return _("Yes");
        if (raw == "false" || raw == "0")
            return _("No");
    }

    return raw;
}

//-----------------------------------------------------------------------------
// The table
//-----------------------------------------------------------------------------

namespace
{

typedef ConfigOption::Category C;
typedef ConfigOption::Type T;

std::vector<ConfigOption> buildTable()
{
    const int cServer = ConfigOption::Server;
    const int cHome = ConfigOption::CalaosHome;
    const int cMcp = ConfigOption::McpSidecar;

    std::vector<ConfigOption> t;
    auto add = [&t](const ConfigOption &o) { t.push_back(o); };

    //--- Authentication ----------------------------------------------------

    add(ConfigOption("cn_user", C::Auth, T::String)
        .label(N_("API user name"))
        .doc(N_("User name every client must present to the JSON API: Calaos Home, Calaos "
                "Installer, the mobile application and the MCP sidecar. It is read on every "
                "request, so a change takes effect at once on the server, but every client has "
                "to be updated too. A freshly created local_config.xml is seeded with \"user\"."))
        .def("user")
        .consumer(cServer | cHome)
        .confirmReset()
        .seeAlso("calaos_user"));

    add(ConfigOption("cn_pass", C::Auth, T::Password)
        .label(N_("API password"))
        .doc(N_("Password every API client must present. Changing it locks out Calaos Home, "
                "Calaos Installer and the mobile application until they are updated too. It is "
                "stored in clear text, which is why local_config.xml is kept readable by its "
                "owner only. A freshly created file is seeded with \"pass\" — change it."))
        .def("pass")
        .secret()
        .confirmReset()
        .consumer(cServer | cHome)
        .seeAlso("calaos_password"));

    add(ConfigOption("calaos_user", C::Auth, T::String)
        .label(N_("API user name (legacy)"))
        .doc(N_("Older name of the API user, still honoured when cn_user is empty. The server "
                "deletes it as soon as the credentials are changed through the API. Keep it "
                "only while an old client is still around."))
        .deprecated("cn_user")
        .advanced()
        .seeAlso("cn_user"));

    add(ConfigOption("calaos_password", C::Auth, T::Password)
        .label(N_("API password (legacy)"))
        .doc(N_("Older name of the API password, still honoured when cn_pass is empty. The "
                "server deletes it as soon as the credentials are changed through the API."))
        .secret()
        .deprecated("cn_pass")
        .advanced()
        .seeAlso("cn_pass"));

    //--- Network -----------------------------------------------------------

    add(ConfigOption("port_api", C::Network, T::Port)
        .label(N_("API port"))
        .doc(N_("TCP port the JSON HTTP/WebSocket API listens on. Changing it requires "
                "reconfiguring every client: Calaos Home, Calaos Installer, the mobile "
                "application and any reverse proxy in front of the server. The MCP sidecar "
                "reads the same key to know where to reach the API."))
        .def("5454")
        .range(1, 65535)
        .restartRequired());

    add(ConfigOption("listen_address", C::Network, T::Host)
        .label(N_("Listen address"))
        .doc(N_("Local address the HTTP/WebSocket API, the UDP discovery server and the "
                "HiFi Rose push notification port bind to. 0.0.0.0 accepts connections on "
                "every interface; set a single address to confine the server to one network. "
                "A value that is not an IP address, or an address that does not exist on the "
                "machine, falls back to 0.0.0.0 and logs a warning naming the refused value. "
                "Confining the listen has a price on the notification port: a HiFi Rose "
                "amplifier pushes to it from the LAN, so an address it cannot reach demotes "
                "its notifications to the fallback poll. The line that announces the "
                "notification listen names the address it bound."))
        .def("0.0.0.0")
        .restartRequired());

    add(ConfigOption("max_http_body_size", C::Network, T::Int)
        .label(N_("Maximum HTTP body size"))
        .doc(N_("Biggest HTTP request body accepted on the API port, in bytes. A "
                "request announcing or sending more is refused with a 413 answer. The "
                "biggest legitimate payload a Calaos client sends is around 215 KiB "
                "(calaos_installer pushing io.xml and rules.xml), the default keeps a "
                "large margin above it. A value that is not a plain number between "
                "4096 and 1073741824 falls back to the default."))
        .def("4194304")
        .range(4096, 1073741824)
        .advanced()
        .restartRequired()
        .seeAlso("max_websocket_message_size"));

    add(ConfigOption("max_websocket_message_size", C::Network, T::Int)
        .label(N_("Maximum websocket message size"))
        .doc(N_("Biggest websocket message accepted on the API port, fragments "
                "included, in bytes. A bigger message is refused with a 1009 close "
                "frame. A single websocket frame stays capped at 4 MiB whatever this "
                "value, so raising it above that only takes effect on fragmented "
                "messages. A value that is not a plain number between 4096 and "
                "1073741824 falls back to the default."))
        .def("4194304")
        .range(4096, 1073741824)
        .advanced()
        .restartRequired()
        .seeAlso("max_http_body_size"));

    add(ConfigOption("max_connections", C::Network, T::Int)
        .label(N_("Maximum connections"))
        .doc(N_("Simultaneous connections accepted on the API port, all clients "
                "together. Above it a new connection is answered 503 and closed right "
                "away, nothing already opened is evicted. A value that is not a plain "
                "number between 1 and 10000 falls back to the default."))
        .def("100")
        .range(1, 10000)
        .advanced()
        .restartRequired()
        .seeAlso("max_connections_per_ip"));

    add(ConfigOption("max_connections_per_ip", C::Network, T::Int)
        .label(N_("Maximum connections per client"))
        .doc(N_("Simultaneous connections accepted from one client address, so that a "
                "single client cannot occupy every max_connections slot and evict "
                "everybody else. Above it a request is answered 429 and its connection "
                "closed. The client address is the last entry of the last "
                "X-Forwarded-For header line (the address the haproxy in front of "
                "calaos_server saw), or the TCP peer address on a direct connection. "
                "A value that is not a plain number between 1 and 10000 falls back to "
                "the default."))
        .def("50")
        .range(1, 10000)
        .advanced()
        .restartRequired()
        .seeAlso("max_connections"));

    add(ConfigOption("request_read_timeout", C::Network, T::Int)
        .label(N_("Request read timeout"))
        .doc(N_("Delay, in seconds, a new connection is given to send one complete "
                "HTTP request before being closed. It only covers the time before the "
                "first request is parsed, so it never applies to an opened websocket, "
                "a long poll or a camera stream, only to a client that connects and "
                "then sends nothing or dribbles its headers. A value that is not a "
                "plain number between 1 and 600 falls back to the default."))
        .def("30")
        .range(1, 600)
        .advanced()
        .restartRequired());

    add(ConfigOption("wwwroot", C::Network, T::Path)
        .label(N_("Web interface directory"))
        .doc(N_("Directory served under /app/ for the main web interface. Leave it empty to "
                "serve the files installed with the server; a path that is not a directory is "
                "ignored and the installed one is used instead."))
        .defDynamic(N_("<data dir>/app")));

    add(ConfigOption("debug_enabled", C::Network, T::Bool)
        .label(N_("Debug web interface"))
        .doc(N_("Serves the debug web interface under /debug/. It exposes the internal state of "
                "the server and is meant for development, so leave it off on a box in "
                "production. When off, /debug/ answers as if it did not exist."))
        .def("false")
        .seeAlso("debug_wwwroot"));

    add(ConfigOption("debug_wwwroot", C::Network, T::Path)
        .label(N_("Debug interface directory"))
        .doc(N_("Directory served under /debug/ when the debug interface is enabled. Leave it "
                "empty to serve the files installed with the server."))
        .defDynamic(N_("<data dir>/debug"))
        .advanced()
        .seeAlso("debug_enabled"));

    //--- Logging -----------------------------------------------------------

    add(ConfigOption("debug_level", C::Logging, T::Int)
        .label(N_("Log level"))
        .doc(N_("Default verbosity of every log domain: 0 unknown, 1 critical, 2 error, "
                "3 warning, 4 info, 5 debug. A value outside that range falls back to 4. The "
                "level is also handed to the external drivers and to the MCP sidecar through "
                "the CALAOS_LOG_LEVEL environment variable."))
        .def("4")
        .range(0, 5)
        .values({ { "0", N_("0 — unknown") },
                  { "1", N_("1 — critical") },
                  { "2", N_("2 — error") },
                  { "3", N_("3 — warning") },
                  { "4", N_("4 — info") },
                  { "5", N_("5 — debug") } })
        .seeAlso("debug_domains"));

    add(ConfigOption("debug_domains", C::Logging, T::Csv)
        .label(N_("Per-domain log levels"))
        .doc(N_("Verbosity of individual log domains, overriding the global level, as a comma "
                "separated list of domain:level pairs. Domains that are not listed keep the "
                "global level. The list is also handed to the external drivers and to the MCP "
                "sidecar through the CALAOS_LOG_DOMAINS environment variable."))
        .pattern("^[A-Za-z0-9_.-]+:[0-5](,[A-Za-z0-9_.-]+:[0-5])*$")
        .example("hifirose:5,network:0")
        .advanced()
        .seeAlso("debug_level"));

    //--- SMTP --------------------------------------------------------------

    add(ConfigOption("smtp_server", C::Smtp, T::Host)
        .label(N_("SMTP server"))
        .doc(N_("Host name or IP address of the SMTP server used to send the notification "
                "e-mails. An old style smtp:// or smtps:// URI is still accepted and reduced to "
                "its host part. While it is empty every mail fails."))
        .example("smtp.example.org"));

    add(ConfigOption("smtp_port", C::Smtp, T::Port)
        .label(N_("SMTP port"))
        .doc(N_("TCP port of the SMTP server: usually 25 for plain SMTP, 587 for submission "
                "with STARTTLS and 465 for implicit TLS. There is no fallback, mail sending "
                "needs this key set."))
        .range(1, 65535)
        .example("587"));

    add(ConfigOption("smtp_auth", C::Smtp, T::Bool)
        .label(N_("SMTP authentication"))
        .doc(N_("Authenticates on the SMTP server with smtp_username and smtp_password. When "
                "off the mail is sent anonymously and both credentials are ignored, which only "
                "works with a relay that trusts the local network."))
        .def("false")
        .seeAlso("smtp_username")
        .seeAlso("smtp_password"));

    add(ConfigOption("smtp_tls", C::Smtp, T::Bool)
        .label(N_("SMTP over TLS"))
        .doc(N_("Opens the connection to the SMTP server over TLS instead of plain text. "
                "Required by nearly every provider, and mandatory whenever credentials travel "
                "outside the local network."))
        .def("false"));

    add(ConfigOption("smtp_username", C::Smtp, T::String)
        .label(N_("SMTP user name"))
        .doc(N_("User name presented to the SMTP server. Only used when smtp_auth is on. Many "
                "providers expect the full e-mail address here."))
        .seeAlso("smtp_auth"));

    add(ConfigOption("smtp_password", C::Smtp, T::Password)
        .label(N_("SMTP password"))
        .doc(N_("Password presented to the SMTP server. Only used when smtp_auth is on. It is "
                "stored in clear text in local_config.xml; prefer a dedicated application "
                "password when the provider offers one."))
        .secret()
        .seeAlso("smtp_auth"));

    add(ConfigOption("smtp_debug", C::Smtp, T::Bool)
        .label(N_("SMTP debug log"))
        .doc(N_("Runs the mail helper in verbose mode so the whole SMTP dialogue is written to "
                "the log. Useful to diagnose a delivery failure, but it also prints the "
                "authentication exchange, so turn it back off afterwards."))
        .def("false")
        .advanced());

    //--- Notifications -----------------------------------------------------

    add(ConfigOption("notif/mail_sender", C::Notifications, T::Email)
        .label(N_("Notification sender"))
        .doc(N_("Address used as the sender of the notification e-mails when a rule does not "
                "give one. Most providers reject a sender that is not one of their own "
                "mailboxes, so the built-in fallback calaos@localhost rarely gets delivered."))
        .def("calaos@localhost"));

    add(ConfigOption("notif/mail_recipients", C::Notifications, T::EmailList)
        .label(N_("Notification recipients"))
        .doc(N_("Destination of the notification e-mails when a rule does not give one. Mail "
                "sending is aborted when this is empty. Beware that the mail helper currently "
                "hands the whole value to the server as a single recipient, so only one "
                "address really gets delivered."))
        .seeAlso("user_emails"));

    add(ConfigOption("notif/battery_mail_enabled", C::Notifications, T::Bool)
        .label(N_("Low battery e-mail"))
        .doc(N_("Sends an e-mail when a battery powered device reports a low battery. Applies "
                "to every IO exposing a battery level. Enabled unless it is explicitly set to "
                "false."))
        .def("true")
        .seeAlso("notif/battery_push_enabled"));

    add(ConfigOption("notif/battery_push_enabled", C::Notifications, T::Bool)
        .label(N_("Low battery push"))
        .doc(N_("Sends a push notification to the registered mobile applications when a battery "
                "powered device reports a low battery."))
        .def("true")
        .seeAlso("notif/battery_mail_enabled"));

    add(ConfigOption("notif/io_connected_mail_enabled", C::Notifications, T::Bool)
        .label(N_("IO connection e-mail"))
        .doc(N_("Sends an e-mail when an IO goes offline or comes back online. This can be very "
                "noisy on an unstable radio network, where a device may flap several times an "
                "hour."))
        .def("true")
        .seeAlso("notif/io_connected_push_enabled"));

    add(ConfigOption("notif/io_connected_push_enabled", C::Notifications, T::Bool)
        .label(N_("IO connection push"))
        .doc(N_("Sends a push notification to the registered mobile applications when an IO "
                "goes offline or comes back online."))
        .def("true")
        .seeAlso("notif/io_connected_mail_enabled"));

    add(ConfigOption("notif_development", C::Notifications, T::Bool)
        .label(N_("Apple development push"))
        .doc(N_("Marks the Apple push notifications as targeting the APNs development gateway "
                "instead of the production one. Only useful with a mobile application built and "
                "signed for development; a production application stops receiving anything. "
                "Note that this key has no notif/ prefix, unlike the other ones here."))
        .def("false")
        .advanced());

    //--- InfluxDB ----------------------------------------------------------

    add(ConfigOption("influxdb_enabled", C::InfluxDb, T::Bool)
        .label(N_("Enable InfluxDB logging"))
        .doc(N_("Logs the state of every IO to an InfluxDB time series database, for graphing "
                "and long term analysis. Everything else in this section is ignored while it is "
                "off."))
        .def("false")
        .restartRequired());

    add(ConfigOption("influxdb_version", C::InfluxDb, T::Enum)
        .label(N_("InfluxDB API version"))
        .doc(N_("InfluxDB 1.x is addressed with a database name; 2.x uses an organisation, a "
                "bucket and an API token. Choosing the wrong one makes every write fail with an "
                "authentication or a not-found error."))
        .def("1")
        .values({ { "1", N_("1.x (database)") },
                  { "2", N_("2.x (org/bucket/token)") } })
        .restartRequired()
        .seeAlso("influxdb_database")
        .seeAlso("influxdb_org")
        .seeAlso("influxdb_bucket")
        .seeAlso("influxdb_token"));

    add(ConfigOption("influxdb_host", C::InfluxDb, T::Host)
        .label(N_("InfluxDB host"))
        .doc(N_("Host name or IP address of the InfluxDB server. The default points at an "
                "instance running on the box itself."))
        .def("127.0.0.1")
        .restartRequired());

    add(ConfigOption("influxdb_port", C::InfluxDb, T::Port)
        .label(N_("InfluxDB port"))
        .doc(N_("TCP port of the InfluxDB HTTP API. 8086 is the default of both 1.x and 2.x."))
        .def("8086")
        .range(1, 65535)
        .restartRequired());

    add(ConfigOption("influxdb_log_timeout", C::InfluxDb, T::Int)
        .label(N_("InfluxDB snapshot interval"))
        .doc(N_("Delay in seconds between two full dumps of every IO state to the database. "
                "States are also written as they change; this periodic snapshot is what keeps a "
                "flat curve alive between two changes. A value of 0 is treated as 300."))
        .def("300")
        .range(0, 86400)
        .restartRequired()
        .advanced());

    add(ConfigOption("influxdb_database", C::InfluxDb, T::String)
        .label(N_("InfluxDB database (1.x)"))
        .doc(N_("Name of the InfluxDB 1.x database the server writes to. The server creates it "
                "at start-up if it does not exist yet. Ignored when the API version is 2."))
        .def("calaos")
        .restartRequired()
        .seeAlso("influxdb_version"));

    add(ConfigOption("influxdb_org", C::InfluxDb, T::String)
        .label(N_("InfluxDB organisation (2.x)"))
        .doc(N_("InfluxDB 2.x organisation owning the bucket. Ignored when the API version "
                "is 1."))
        .def("calaos")
        .restartRequired()
        .seeAlso("influxdb_version"));

    add(ConfigOption("influxdb_bucket", C::InfluxDb, T::String)
        .label(N_("InfluxDB bucket (2.x)"))
        .doc(N_("InfluxDB 2.x bucket the measurements are written to. It has to exist already, "
                "the server does not create it. Ignored when the API version is 1."))
        .def("calaos-data")
        .restartRequired()
        .seeAlso("influxdb_version"));

    add(ConfigOption("influxdb_token", C::InfluxDb, T::Token)
        .label(N_("InfluxDB token (2.x)"))
        .doc(N_("InfluxDB 2.x API token with write access to the bucket. Data logging stays "
                "disabled while it is empty, and the server reports an unauthorized error when "
                "it is wrong. Ignored when the API version is 1."))
        .secret()
        .restartRequired()
        .seeAlso("influxdb_version"));

    //--- History -----------------------------------------------------------

    add(ConfigOption("history_keep_days", C::History, T::Int)
        .label(N_("History retention"))
        .doc(N_("Number of days of event history kept in the local database. Older events are "
                "deleted every time a new one is recorded, together with the pictures the push "
                "notifications attached to them — lowering this value destroys data at the next "
                "event."))
        .def("30")
        .range(1, 3650));

    //--- Location ----------------------------------------------------------

    add(ConfigOption("latitude", C::Location, T::Float)
        .label(N_("Latitude"))
        .doc(N_("Latitude in decimal degrees, used to compute sunrise and sunset for the time "
                "ranges of the rules, and by Calaos Home for the weather. The default shown "
                "here is the one the sunrise computation really falls back to when the key is "
                "missing (centre of France); a freshly created local_config.xml is instead "
                "seeded with 48.864715, Paris."))
        .def("46.422713")
        .range(-90, 90)
        .consumer(cServer | cHome)
        .seeAlso("longitude"));

    add(ConfigOption("longitude", C::Location, T::Float)
        .label(N_("Longitude"))
        .doc(N_("Longitude in decimal degrees, used to compute sunrise and sunset for the time "
                "ranges of the rules, and by Calaos Home for the weather. The default shown "
                "here is the one the sunrise computation really falls back to when the key is "
                "missing (centre of France); a freshly created local_config.xml is instead "
                "seeded with 2.322235, Paris."))
        .def("2.548828")
        .range(-180, 180)
        .consumer(cServer | cHome)
        .seeAlso("latitude"));

    //--- MCP ---------------------------------------------------------------

    add(ConfigOption("mcp_token", C::Mcp, T::Token)
        .label(N_("MCP client token"))
        .doc(N_("Token an MCP client has to present to reach the Calaos MCP server. 64 "
                "hexadecimal characters generated at the first start and written back to "
                "local_config.xml. Emptying it makes the server generate a new one at the next "
                "start, which invalidates every client already configured."))
        .secret()
        .generated()
        .restartRequired()
        .pattern("^[0-9a-fA-F]{64}$")
        .consumer(cServer | cMcp)
        .seeAlso("mcp_service_token"));

    add(ConfigOption("mcp_service_token", C::Mcp, T::Token)
        .label(N_("MCP service token"))
        .doc(N_("Token the MCP sidecar uses to authenticate itself against the JSON API of the "
                "server. Generated the same way as mcp_token at the first start. It is purely "
                "internal, no external client ever needs it."))
        .secret()
        .generated()
        .restartRequired()
        .pattern("^[0-9a-fA-F]{64}$")
        .consumer(cServer | cMcp)
        .seeAlso("mcp_token"));

    add(ConfigOption("mcp_rate_limit", C::Mcp, T::Int)
        .label(N_("MCP rate limit"))
        .doc(N_("Maximum number of authentication attempts the MCP sidecar accepts from one IP "
                "address before it starts refusing them. 0 disables the limit. Read by the "
                "Python sidecar only, never by calaos_server."))
        .def("300")
        .range(0, 100000)
        .consumer(cMcp)
        .restartRequired()
        .advanced());

    add(ConfigOption("mcp_ban_failures", C::Mcp, T::Int)
        .label(N_("MCP ban threshold"))
        .doc(N_("Number of failed authentications from one IP address before the MCP sidecar "
                "bans it for mcp_ban_seconds. 0 disables banning entirely. Read by the Python "
                "sidecar only, never by calaos_server."))
        .def("20")
        .range(0, 10000)
        .consumer(cMcp)
        .restartRequired()
        .advanced()
        .seeAlso("mcp_ban_seconds"));

    add(ConfigOption("mcp_ban_seconds", C::Mcp, T::Int)
        .label(N_("MCP ban duration"))
        .doc(N_("How long, in seconds, an IP address stays banned once it reached "
                "mcp_ban_failures failed authentications. Read by the Python sidecar only, "
                "never by calaos_server."))
        .def("120")
        .range(0, 86400)
        .consumer(cMcp)
        .restartRequired()
        .advanced()
        .seeAlso("mcp_ban_failures"));

    //--- RemoteUI / OTA ----------------------------------------------------

    add(ConfigOption("ota_enabled", C::Ota, T::Bool)
        .label(N_("Firmware updates over the air"))
        .doc(N_("Offers firmware updates to the RemoteUI touch screens: the server scans the "
                "firmware directory and proposes the newest build to every screen that "
                "connects. For historical reasons the value 1 is accepted as well as true."))
        .def("true")
        .values({ { "true", N_("Yes") },
                  { "1", N_("Yes") },
                  { "false", N_("No") },
                  { "0", N_("No") } })
        .restartRequired());

    add(ConfigOption("ota_firmware_path", C::Ota, T::Path)
        .label(N_("Firmware directory"))
        .doc(N_("Directory scanned for the RemoteUI firmware images offered over the air. Leave "
                "it empty to use the directory created by the installation."))
        .defDynamic(N_("<data dir>/firmwares"))
        .restartRequired());

    add(ConfigOption("ota_rescan_interval", C::Ota, T::Int)
        .label(N_("Firmware rescan interval"))
        .doc(N_("Delay in minutes between two scans of the firmware directory. Anything below 1 "
                "is treated as 60. Lower it while publishing new firmwares, raise it to spare a "
                "slow storage."))
        .def("60")
        .range(1, 10080)
        .restartRequired());

    //--- Calaos Home -------------------------------------------------------

    add(ConfigOption("show_cursor", C::CalaosHome, T::Bool)
        .label(N_("Show the mouse pointer"))
        .doc(N_("Shows the real X11 pointer in Calaos Home. When off it is replaced by a 1x1 "
                "transparent pixmap, which is what a touch screen wants. calaos_server only "
                "seeds this key, Calaos Home is the one reading it."))
        .def("true")
        .consumer(cHome));

    add(ConfigOption("dpms_enable", C::CalaosHome, T::Bool)
        .label(N_("Automatic screen blanking"))
        .doc(N_("Lets Calaos Home blank the touch screen after a period without any touch. "
                "calaos_server only seeds this key, Calaos Home is the one reading it."))
        .def("false")
        .consumer(cHome)
        .seeAlso("dpms_standby"));

    add(ConfigOption("dpms_standby", C::CalaosHome, T::Int)
        .label(N_("Blanking delay"))
        .doc(N_("Idle delay in minutes before Calaos Home blanks the screen, when automatic "
                "blanking is on. Any value below 1 is treated as 1 minute. Read by Calaos Home "
                "only."))
        //0 is accepted on purpose: Calaos Home maps it to 1 minute, and a validator that
        //refuses a value the code handles would flag an existing config as invalid.
        .range(0, 1440)
        .consumer(cHome)
        .seeAlso("dpms_enable"));

    add(ConfigOption("calaos_server_host", C::CalaosHome, T::Host)
        .label(N_("Server address"))
        .doc(N_("Forces the address Calaos Home connects to. Setting it disables the UDP "
                "auto-discovery on port 4545, which is what you want when the screen and the "
                "server sit on different subnets or when several servers answer. Leave it empty "
                "to keep auto-discovery. Read by Calaos Home only."))
        .consumer(cHome)
        .seeAlso("calaos/host"));

    add(ConfigOption("lang", C::CalaosHome, T::Enum)
        .label(N_("Interface language"))
        .doc(N_("Language of the Calaos Home interface. Leave it empty to follow the system "
                "locale. Read by Calaos Home only, it has no effect on the language of the "
                "server logs."))
        .values({ { "de", N_("German") },
                  { "en", N_("English") },
                  { "es", N_("Spanish") },
                  { "fr", N_("French") },
                  { "hi", N_("Hindi") },
                  { "nb", N_("Norwegian Bokmål") },
                  { "pl", N_("Polish") },
                  { "ru", N_("Russian") } })
        .consumer(cHome));

    add(ConfigOption("user_emails", C::CalaosHome, T::EmailList)
        .label(N_("User e-mail addresses"))
        .doc(N_("Addresses managed from the \"user info\" page of Calaos Home. It overlaps "
                "notif/mail_recipients without being the same key: the server never reads this "
                "one and Calaos Home never reads the other, so both have to be kept in step by "
                "hand."))
        .consumer(cHome)
        .seeAlso("notif/mail_recipients"));

    add(ConfigOption("calaos/host", C::CalaosHome, T::Host)
        .label(N_("Server address (stray key)"))
        .doc(N_("Known pollution rather than a real option. Calaos Home saves its settings "
                "through QSettings on every platform and, on the desktop build, the group path "
                "of the setting leaks verbatim into local_config.xml. Nothing ever reads it "
                "back; the address really used is calaos_server_host. It is deliberately left "
                "alone by the automatic purge, so deleting it is safe but manual."))
        .deprecated("calaos_server_host")
        .advanced()
        .consumer(cHome)
        .seeAlso("calaos_server_host"));

    return t;
}

const char *categoryId(C c)
{
    switch (c)
    {
    case C::Auth: return "auth";
    case C::Network: return "network";
    case C::Logging: return "logging";
    case C::Smtp: return "smtp";
    case C::Notifications: return "notifications";
    case C::InfluxDb: return "influxdb";
    case C::History: return "history";
    case C::Location: return "location";
    case C::Mcp: return "mcp";
    case C::Ota: return "ota";
    case C::CalaosHome: return "calaos_home";
    case C::Unknown: return "unknown";
    }
    return "unknown";
}

const char *typeId(T t)
{
    switch (t)
    {
    case T::String: return "string";
    case T::Bool: return "bool";
    case T::Int: return "int";
    case T::Float: return "float";
    case T::Port: return "port";
    case T::Host: return "host";
    case T::Path: return "path";
    case T::Email: return "email";
    case T::EmailList: return "email_list";
    case T::Password: return "password";
    case T::Token: return "token";
    case T::Enum: return "enum";
    case T::Csv: return "csv";
    }
    return "string";
}

//Levenshtein distance, used by suggest()
int editDistance(const std::string &a, const std::string &b)
{
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);

    for (std::size_t j = 0; j <= b.size(); j++)
        prev[j] = static_cast<int>(j);

    for (std::size_t i = 1; i <= a.size(); i++)
    {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); j++)
        {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min(std::min(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        prev = cur;
    }

    return prev[b.size()];
}

std::string escapeMarkdownCell(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c: s)
    {
        if (c == '|')
            out += "\\|";
        else if (c == '\n' || c == '\r')
            out += ' ';
        else
            out += c;
    }
    return out;
}

} //namespace

//-----------------------------------------------------------------------------
// ConfigOptions
//-----------------------------------------------------------------------------

const std::vector<ConfigOption> &ConfigOptions::all()
{
    static const std::vector<ConfigOption> table = buildTable();
    return table;
}

const ConfigOption *ConfigOptions::find(const std::string &key)
{
    for (const ConfigOption &o: all())
    {
        if (o.key() == key)
            return &o;
    }
    return nullptr;
}

std::vector<const ConfigOption *> ConfigOptions::byCategory(ConfigOption::Category c)
{
    std::vector<const ConfigOption *> res;
    for (const ConfigOption &o: all())
    {
        if (o.category() == c)
            res.push_back(&o);
    }
    return res;
}

std::vector<ConfigOption::Category> ConfigOptions::categories()
{
    return { C::Network, C::Auth, C::Logging, C::Smtp, C::Notifications,
             C::InfluxDb, C::History, C::Location, C::Mcp, C::Ota,
             C::CalaosHome, C::Unknown };
}

std::string ConfigOptions::categoryLabel(ConfigOption::Category c)
{
    switch (c)
    {
    case C::Auth: return _("Authentication");
    case C::Network: return _("Network / API");
    case C::Logging: return _("Logging");
    case C::Smtp: return _("E-mail (SMTP)");
    case C::Notifications: return _("Notifications");
    case C::InfluxDb: return _("InfluxDB");
    case C::History: return _("History");
    case C::Location: return _("Location");
    case C::Mcp: return _("MCP");
    case C::Ota: return _("RemoteUI / OTA");
    case C::CalaosHome: return _("Calaos Home");
    case C::Unknown: return _("Undocumented");
    }
    return _("Undocumented");
}

std::string ConfigOptions::typeLabel(ConfigOption::Type t)
{
    switch (t)
    {
    case T::String: return _("text");
    case T::Bool: return _("boolean");
    case T::Int: return _("whole number");
    case T::Float: return _("decimal number");
    case T::Port: return _("TCP port");
    case T::Host: return _("host name or IP address");
    case T::Path: return _("filesystem path");
    case T::Email: return _("e-mail address");
    case T::EmailList: return _("list of e-mail addresses");
    case T::Password: return _("password");
    case T::Token: return _("token");
    case T::Enum: return _("choice");
    case T::Csv: return _("comma separated list");
    }
    return _("text");
}

std::string ConfigOptions::consumerLabel(int mask)
{
    std::vector<std::string> parts;

    if (mask & ConfigOption::Server)
        parts.push_back(_("calaos_server"));
    if (mask & ConfigOption::CalaosHome)
        parts.push_back(_("Calaos Home"));
    if (mask & ConfigOption::McpSidecar)
        parts.push_back(_("MCP sidecar"));

    if (parts.empty())
        return _("nobody");

    std::string res;
    for (std::size_t i = 0; i < parts.size(); i++)
    {
        if (i)
            res += ", ";
        res += parts[i];
    }
    return res;
}

std::string ConfigOptions::suggest(const std::string &unknownKey)
{
    if (unknownKey.empty())
        return std::string();

    std::string best;
    int bestDist = -1;

    for (const ConfigOption &o: all())
    {
        int d = editDistance(unknownKey, o.key());
        if (bestDist < 0 || d < bestDist)
        {
            bestDist = d;
            best = o.key();
        }
    }

    if (bestDist < 0)
        return std::string();

    //Close enough to be a typo, and not just "the least distant of a bad lot"
    std::size_t longest = std::max(unknownKey.size(), best.size());
    if (bestDist > 3 || static_cast<std::size_t>(bestDist) * 2 >= longest)
        return std::string();

    return best;
}

const std::vector<std::string> &ConfigOptions::obsoleteKeys()
{
    /* Removed from the code by commit 8684877b. They are kept here so that the
     * tools can recognise and purge them, and they must never be added back to
     * the table above.
     */
    static const std::vector<std::string> keys =
    {
        "hwid",
        "use_ntp",
        "fw_target",
        "fw_version",
        "device_type",
    };
    return keys;
}

bool ConfigOptions::isObsolete(const std::string &key)
{
    const std::vector<std::string> &keys = obsoleteKeys();
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

std::vector<std::string> ConfigOptions::obsoletePresent()
{
    std::vector<std::string> present;
    Params opts;

    if (!Utils::get_config_options(opts))
        return present;

    for (const std::string &key: obsoleteKeys())
    {
        if (opts.Exists(key))
            present.push_back(key);
    }

    return present;
}

bool ConfigOptions::purgeObsolete(std::vector<std::string> *removed)
{
    if (removed)
        removed->clear();

    std::vector<std::string> present = obsoletePresent();
    if (present.empty())
        return true;

    /* A read-only rootfs is a legitimate deployment: skip silently instead of
     * logging an error on every start. Nothing else in this function can
     * throw, get_config_options() and set_config_options() both report by
     * return value.
     */
    string file = Utils::getConfigFile(LOCAL_CONFIG);
    if (!FileUtils::isWritable(file) || !FileUtils::isWritable(Utils::getConfigFile("")))
    {
        cDebugDom("config") << "Obsolete options found but " << file
                            << " is not writable, nothing purged";
        return false;
    }

    //Only the obsolete keys are touched, every other key of the file is
    //reloaded and kept by set_config_options()
    if (!Utils::set_config_options(Params(), present))
    {
        cErrorDom("config") << "Unable to purge the obsolete options of " << file;
        return false;
    }

    if (removed)
        *removed = present;

    return true;
}

std::string ConfigOptions::genMarkdown()
{
    std::ostringstream out;

    out << "# " << _("Configuration options") << "\n\n";
    out << _("This page is generated from the option registry of "
             "`src/lib/ConfigOptions.cpp`, do not edit it by hand.") << "\n\n";
    out << _("`local_config.xml` is a shared file: calaos_server is not its only reader. "
             "Calaos Home, the touch screen interface, reads and writes the same file, and "
             "the MCP sidecar reads a few keys of its own. The \"Used by\" column says who "
             "actually consumes each key.") << "\n\n";
    out << _("Annotations: **⟳** restart required, **🔒** secret, **⚙** generated "
             "automatically, **⚠** deprecated, **◦** advanced.") << "\n";

    for (ConfigOption::Category c: categories())
    {
        std::vector<const ConfigOption *> opts = byCategory(c);
        if (opts.empty())
            continue;

        out << "\n## " << categoryLabel(c) << "\n\n";
        out << "| " << _("Key") << " | " << _("Type") << " | " << _("Default")
            << " | " << _("Used by") << " | " << _("Description") << " |\n";
        out << "|---|---|---|---|---|\n";

        for (const ConfigOption *o: opts)
        {
            std::string defCell;
            if (o->isGenerated())
                defCell = _("generated");
            else if (o->hasDef())
                defCell = o->def().empty() ? std::string(_("empty")) : "`" + o->def() + "`";
            else if (!o->defDynamic().empty())
                defCell = "`" + o->defDynamic() + "`";
            else
                defCell = _("empty");

            std::string desc = o->doc();

            if (o->hasRange())
            {
                desc += " " + std::string(_("Range:")) + " " + formatNumber(o->rangeMin()) +
                        "…" + formatNumber(o->rangeMax()) + ".";
            }
            if (!o->values().empty())
            {
                std::string list;
                for (const auto &kv: o->values())
                {
                    if (!list.empty())
                        list += ", ";
                    list += "`" + kv.first + "`";
                }
                desc += " " + std::string(_("Accepted values:")) + " " + list + ".";
            }
            if (o->example())
                desc += " " + std::string(_("Example:")) + " `" + o->example() + "`.";
            if (o->isDeprecated())
            {
                desc += " ⚠ " + std::string(_("Deprecated."));
                if (!o->replacement().empty())
                    desc += " " + std::string(_("Use")) + " `" + o->replacement() + "` " +
                            _("instead") + ".";
            }
            if (!o->seeAlso().empty())
            {
                std::string list;
                for (const std::string &k: o->seeAlso())
                {
                    if (!list.empty())
                        list += ", ";
                    list += "`" + k + "`";
                }
                desc += " " + std::string(_("See also:")) + " " + list + ".";
            }

            std::string flags;
            if (o->isRestartRequired()) flags += " ⟳";
            if (o->isSecret()) flags += " 🔒";
            if (o->isGenerated()) flags += " ⚙";
            if (o->isDeprecated()) flags += " ⚠";
            if (o->isAdvanced()) flags += " ◦";

            out << "| `" << o->key() << "`" << flags
                << " | " << escapeMarkdownCell(typeLabel(o->type()))
                << " | " << escapeMarkdownCell(defCell)
                << " | " << escapeMarkdownCell(consumerLabel(o->consumer()))
                << " | " << escapeMarkdownCell(o->label() + " — " + desc)
                << " |\n";
        }
    }

    out << "\n## " << _("Obsolete keys") << "\n\n";
    out << _("These keys are no longer read by anything. `calaos_config purge` removes "
             "them, and calaos_server removes them at start-up when the configuration is "
             "writable.") << "\n\n";
    for (const std::string &k: obsoleteKeys())
        out << "- `" << k << "`\n";

    return out.str();
}

std::string ConfigOptions::genJson()
{
    Json doc;
    doc["categories"] = Json::array();
    doc["options"] = Json::array();
    doc["obsolete_keys"] = Json::array();

    for (ConfigOption::Category c: categories())
    {
        doc["categories"].push_back({
            { "id", categoryId(c) },
            { "label", categoryLabel(c) },
        });
    }

    for (const ConfigOption &o: all())
    {
        Json j;
        j["key"] = o.key();
        j["category"] = categoryId(o.category());
        j["type"] = typeId(o.type());
        j["label"] = o.label();
        j["doc"] = o.doc();

        if (o.hasDef())
            j["default"] = o.def();
        if (!o.defDynamic().empty())
            j["default_dynamic"] = o.defDynamic();
        if (o.hasRange())
        {
            j["min"] = o.rangeMin();
            j["max"] = o.rangeMax();
        }
        if (!o.values().empty())
        {
            j["values"] = Json::array();
            for (const auto &kv: o.values())
            {
                j["values"].push_back({
                    { "value", kv.first },
                    { "label", o.valueLabel(kv.first) },
                });
            }
        }
        if (o.pattern())
            j["pattern"] = o.pattern();
        if (o.example())
            j["example"] = o.example();

        Json consumers = Json::array();
        if (o.consumer() & ConfigOption::Server)
            consumers.push_back("server");
        if (o.consumer() & ConfigOption::CalaosHome)
            consumers.push_back("calaos_home");
        if (o.consumer() & ConfigOption::McpSidecar)
            consumers.push_back("mcp_sidecar");
        j["consumers"] = consumers;

        j["restart_required"] = o.isRestartRequired();
        j["generated"] = o.isGenerated();
        j["secret"] = o.isSecret();
        j["confirm_reset"] = o.isConfirmReset();
        j["deprecated"] = o.isDeprecated();
        j["advanced"] = o.isAdvanced();
        if (!o.replacement().empty())
            j["replacement"] = o.replacement();
        if (!o.seeAlso().empty())
            j["see_also"] = o.seeAlso();

        doc["options"].push_back(j);
    }

    for (const std::string &k: obsoleteKeys())
        doc["obsolete_keys"].push_back(k);

    //E4.1b: error handler only. This text is read back by a parser, and
    //adding ensure_ascii would change the bytes of an artefact E4.1 does not
    //migrate.
    return doc.dump(4, ' ', false, Json::error_handler_t::replace);
}
