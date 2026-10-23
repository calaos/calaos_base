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
#include "JsonApi.h"
#include "AdminEvents.h"
#include "AdminState.h"
#include "DaemonClient.h"
#include "HttpClient.h"
#include "JsonField.h"

#include <cmath>
#include <set>

namespace
{

//An old installer shows `error` as is, so every code reads as a sentence too
string messageFor(const string &code)
{
    static const map<string, string> messages = {
        { "admin_not_set", "Admin password is not set" },
        { "admin_already_set", "Admin password is already set" },
        { "bad_password", "Wrong admin password" },
        { "rate_limited", "Too many attempts, retry later" },
        { "household_password_required", "Change the household password first" },
        { "bad_session", "Admin session required" },
        { "password_too_short", "Admin password is too short" },
        { "https_required", "Use https://" },
        { "daemon_too_old", "Administration is being activated, calaos-container is too old" },
        { "daemon_unreachable", "calaos-container is unreachable" },
        { "admin_unavailable", "Administration unavailable (calaos-container unreachable; only available on Calaos OS)" },
        { "upgrade_in_progress", "An upgrade is already in progress" },
        { "not_allowed", "Action not allowed" },
        { "invalid_request", "Invalid request" },
        { "package_not_found", "Package has no pending update" },
        { "internal", "Internal error" },
    };

    const auto it = messages.find(code);
    return it == messages.end() ? string("Internal error") : it->second;
}

//The optional fields of an error are strings on this wire
string fieldToString(const Json &v)
{
    if (v.is_string())
        return v.get<string>();
    if (v.is_number())
        return std::to_string((long long)std::ceil(v.get<double>()));
    return string();
}

//The daemon's own maximum: a longer one would cost a big loopback transfer for
//an answer that cannot be anything but a refusal
constexpr size_t MaxPasswordBytes = 1024;

//Empty when the password field is usable, the invalid_request message otherwise
string passwordFieldError(const Json &request, const char *key, string &out)
{
    out = JsonField::string(request, key);
    if (out.empty())
        return string(key) + " is required";
    if (out.size() > MaxPasswordBytes)
        return string(key) + " is longer than 1024 bytes";
    return string();
}

//The closed relay table of calaos-container's docs/API.md: nothing of the
//method, the path or the headers comes from the client
struct RelayRow
{
    const char *action;
    const char *method;
    const char *path;
    bool session;
    long timeout;
};

const RelayRow relayTable[] = {
    { "update_status", "GET", "update/status", false, DaemonClient::DefaultTimeout },
    { "update_check", "GET", "update/check", true, DaemonClient::UpdateCheckTimeout },
    { "update_available", "GET", "update/available", true, DaemonClient::DefaultTimeout },
    { "update_upgrade_all", "POST", "update/upgrade-all", true, DaemonClient::DefaultTimeout },
    { "update_upgrade", "POST", "update/upgrade", true, DaemonClient::DefaultTimeout },
    { "system_reboot", "POST", "system/reboot", true, DaemonClient::DefaultTimeout },
    { "system_restart", "POST", "system/restart", true, DaemonClient::RestartTimeout },
    { "system_info", "GET", "system/info", true, DaemonClient::DefaultTimeout },
};

const RelayRow *relayRow(const string &action)
{
    for (const RelayRow &row: relayTable)
    {
        if (action == row.action)
            return &row;
    }
    return nullptr;
}

//update/status without the package inventory
Json householdUpdateStatus(const Json &data)
{
    const Json progress = data.is_object() && data.contains("progress") ? data["progress"] : Json();
    const Json job = data.is_object() && data.contains("job") ? data["job"] : Json();
    return Json{ { "progress", AdminEvents::householdStatus(progress) }, { "job", AdminEvents::householdJob(job) } };
}

Json adminNotSet()
{
    return Json{ { "success", "false" },
                 { "error", "admin_not_set" },
                 { "msg", messageFor("admin_not_set") },
                 { "api_allowed_hosts", Utils::get_config_option("api_allowed_hosts") } };
}

Json setSucceeded(const DaemonReply &reply, bool hasSession)
{
    string mode = JsonField::string(reply.data, "mode");
    if (mode != "bootstrap" && mode != "change")
        mode = hasSession ? "change" : "bootstrap";

    cInfoDom("admin") << "Admin password defined (" << mode << ")";
    return Json{ { "success", "true" }, { "mode", mode } };
}

} //namespace

bool JsonApi::isAdminCommand(const string &command)
{
    return command == "admin_unlock" || command == "admin_set" || command == "admin";
}

Json JsonApi::adminError(const string &code, const string &msg)
{
    return Json{ { "success", "false" },
                 { "error", code },
                 { "msg", msg.empty() ? messageFor(code) : msg } };
}

bool JsonApi::isValidAdminSession(const string &session)
{
    if (session.size() != 64)
        return false;
    for (char c: session)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

bool JsonApi::householdAllowsBootstrap()
{
    //checkCredentials() falls back to calaos_user/calaos_password unless both are set
    const string user = Utils::get_config_option("cn_user");
    const string pass = Utils::get_config_option("cn_pass");

    if (user.empty() || pass.empty())
        return false;

    return !(user == "user" && pass == "pass");
}

Json JsonApi::adminErrorFromReply(const DaemonReply &reply)
{
    switch (reply.kind)
    {
    case DaemonReply::Kind::Unreachable:
        return adminError(AdminState::Instance().flagPresent() ? "daemon_unreachable" : "admin_unavailable", string());
    case DaemonReply::Kind::Bearer:
        return adminError("daemon_unreachable", string());
    case DaemonReply::Kind::TooOld:
        return adminError("daemon_too_old", string());
    case DaemonReply::Kind::Ok:
        cErrorDom("admin") << "calaos-container answered an unexpected payload";
        return adminError("internal", string());
    case DaemonReply::Kind::Error:
        break;
    }

    static const std::set<string> sameCode = { "bad_session", "bad_password", "rate_limited", "password_too_short",
                                               "admin_not_set", "admin_already_set", "upgrade_in_progress",
                                               "not_allowed" };

    if (reply.code == "admin_not_set")
        return adminNotSet();

    if (sameCode.count(reply.code))
    {
        Json j = adminError(reply.code, string());
        static const map<string, string> details = { { "rate_limited", "retry_after" },
                                                     { "upgrade_in_progress", "job_id" },
                                                     { "password_too_short", "min_length" } };
        const auto it = details.find(reply.code);
        if (it != details.end() && reply.data.is_object() && reply.data.contains(it->second))
        {
            const string value = fieldToString(reply.data[it->second]);
            if (!value.empty())
                j[it->second] = value;
        }
        return j;
    }

    //The daemon promises these messages are safe to display
    if (reply.code == "invalid_request" || reply.code == "package_not_found")
        return adminError(reply.code, reply.msg);

    cErrorDom("admin") << "calaos-container answered " << reply.code;
    return adminError("internal", string());
}

bool JsonApi::isValidPackageName(const string &name)
{
    if (name.empty() || name.size() > 128 || name.find("..") != string::npos)
        return false;

    for (size_t i = 0; i < name.size(); i++)
    {
        const char c = name[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && (i == 0 || (c != '.' && c != '+' && c != '-')))
            return false;
    }
    return true;
}

Json JsonApi::adminRequestRefusal(const string &action, const Json &request)
{
    static const std::set<string> phase2 = { "approver_list", "approver_enroll", "approver_confirm", "console_open" };

    if (action == "mcp_regenerate")
        return Json();

    if (!relayRow(action))
        return adminError("not_allowed", phase2.count(action) ? "Not available yet" : "Unknown admin action");

    if (action == "update_upgrade")
    {
        const Json params = request.is_object() && request.contains("params") ? request["params"] : Json();
        const auto it = params.is_object() ? params.find("package") : params.end();
        if (!params.is_object() || it == params.end() || !it->is_string())
            return adminError("invalid_request", "params.package is required");
        if (!isValidPackageName(it->get<string>()))
            return adminError("invalid_request", "Invalid package name");
    }

    return Json();
}

long JsonApi::adminRelayTimeout(const string &action)
{
    const RelayRow *row = relayRow(action);
    return row ? row->timeout : 0;
}

namespace
{

void relayCall(const DaemonClient::Request &req, bool household, bool sessionOptional, const std::function<void(const Json &)> &answer)
{
    DaemonClient::Instance().call(req, [req, household, sessionOptional, answer](const DaemonReply &reply)
                                  {
        if (reply.kind == DaemonReply::Kind::Ok)
        {
            answer(Json{ { "success", "true" }, { "result", household ? householdUpdateStatus(reply.data) : reply.data } });
            return;
        }

        //An expired session on update_status still gets what any household
        //client may read: that is what lets a client resume without the password
        if (sessionOptional && !household && reply.kind == DaemonReply::Kind::Error && reply.code == "bad_session")
        {
            DaemonClient::Request anonymous = req;
            anonymous.session.clear();
            relayCall(anonymous, true, true, answer);
            return;
        }

        if (reply.kind == DaemonReply::Kind::Error && reply.code == "admin_not_set")
            AdminState::Instance().observe(false);
        else if (reply.kind == DaemonReply::Kind::TooOld)
            AdminState::Instance().observeTooOld();

        answer(JsonApi::adminErrorFromReply(reply));
    });
}

} //namespace

void JsonApi::relayAdmin(const string &action, const Json &request, const string &session, const AdminAnswer &answer)
{
    const RelayRow *row = relayRow(action);
    if (!row)
    {
        answer(adminError("not_allowed", "Unknown admin action")); //LCOV_EXCL_LINE refused before
        return;                                                    //LCOV_EXCL_LINE refused before
    }

    //update_status without a session is the household view, which carries no
    //package inventory; the admin's session opens the full answer
    const bool household = !row->session && !isValidAdminSession(session);

    DaemonClient::Request req;
    req.method = row->method;
    req.path = row->path;
    req.timeout = row->timeout;
    req.clientIp = adminClientIp();
    if (!household)
        req.session = session;
    //Built here from the validated name, whatever else params holds
    if (action == "update_upgrade")
        req.body = Json{ { "package", request["params"]["package"].get<string>() } };

    relayCall(req, household, !row->session, answer);
}

bool JsonApi::secureTransport() const
{
    return httpClient && httpClient->secureTransport();
}

string JsonApi::adminClientIp() const
{
    return httpClient ? httpClient->getEffectiveClientIp() : string();
}

JsonApi::AdminAnswer JsonApi::guardedAnswer(const AdminAnswer &answer)
{
    std::weak_ptr<bool> alive = apiAlive;
    return [alive, answer](const Json &j)
    {
        if (!alive.expired())
            answer(j);
        };
}

void JsonApi::withAdminMode(const std::function<void()> &next)
{
    if (AdminState::Instance().mode() == AdminState::Mode::Set)
    {
        next();
        return;
    }

    std::weak_ptr<bool> alive = apiAlive;
    AdminState::Instance().refresh([alive, next]()
                                   {
        if (!alive.expired())
            next();
    });
}

Json JsonApi::adminModeRefusal()
{
    switch (AdminState::Instance().mode())
    {
    case AdminState::Mode::Set: return Json();
    case AdminState::Mode::NotSet: return adminNotSet();
    case AdminState::Mode::TooOld: return adminError("daemon_too_old", string());
    case AdminState::Mode::Closed: return adminError("daemon_unreachable", string());
    case AdminState::Mode::Unavailable: break;
    }
    return adminError("admin_unavailable", string());
}

void JsonApi::validateAdminSession(const string &session, const AdminAnswer &done)
{
    DaemonClient::Request req;
    req.path = "admin/status";
    req.session = session;
    req.clientIp = adminClientIp();

    DaemonClient::Instance().call(req, [done](const DaemonReply &reply)
                                  {
        if (reply.kind == DaemonReply::Kind::Ok && reply.data.is_object() &&
            reply.data.contains("admin_set") && reply.data["admin_set"].is_boolean())
        {
            const bool adminSet = reply.data["admin_set"].get<bool>();
            AdminState::Instance().observe(adminSet);
            done(adminSet ? Json() : adminError("bad_session", string()));
            return;
        }

        if (reply.kind == DaemonReply::Kind::TooOld)
            AdminState::Instance().observeTooOld();

        done(adminErrorFromReply(reply));
    });
}

void JsonApi::processAdminUnlock(const Json &request, const AdminAnswer &answerIn)
{
    const AdminAnswer answer = guardedAnswer(answerIn);

    if (!secureTransport())
    {
        answer(adminError("https_required", string()));
        return;
    }

    //Its own key: a wrong admin password must not lock the household login out
    const string ip = adminClientIp();
    const string key = "admin:" + ip;
    const double now = Utils::getMainLoopTime();
    if (LoginThrottle::isBlocked(key, now))
    {
        Json j = adminError("rate_limited", string());
        j["retry_after"] = std::to_string((long long)std::ceil(LoginThrottle::retryAfter(key, now)));
        answer(j);
        return;
    }

    string password;
    const string invalid = passwordFieldError(request, "admin_pass", password);
    if (!invalid.empty())
    {
        answer(adminError("invalid_request", invalid));
        return;
    }

    withAdminMode([this, answer, password, key, ip]()
                  {
        const Json refusal = adminModeRefusal();
        if (!refusal.is_null())
        {
            answer(refusal);
            return;
        }

        DaemonClient::Request req;
        req.method = "POST";
        req.path = "admin/verify";
        req.body = Json{ { "password", password } };
        req.clientIp = ip;

        DaemonClient::Instance().call(req, [answer, key, ip](const DaemonReply &reply)
                                      {
            if (reply.kind == DaemonReply::Kind::Ok)
            {
                const string session = JsonField::string(reply.data, "session");
                const string expiresIn = reply.data.is_object() && reply.data.contains("expires_in") ? fieldToString(reply.data["expires_in"]) : string();
                if (!isValidAdminSession(session) || expiresIn.empty())
                {
                    cErrorDom("admin") << "admin/verify answered no usable session";
                    answer(adminError("internal", string()));
                    return;
                }

                LoginThrottle::registerSuccess(key);
                answer(Json{ { "success", "true" }, { "admin_session", session }, { "expires_in", expiresIn } });
                return;
            }

            if (reply.kind == DaemonReply::Kind::Error && reply.code == "bad_password")
            {
                cWarningDom("admin") << "Wrong admin password from " << ip;
                LoginThrottle::registerFailure(key, Utils::getMainLoopTime());
            }
            else if (reply.kind == DaemonReply::Kind::Error && reply.code == "admin_not_set")
            {
                AdminState::Instance().observe(false);
            }
            else if (reply.kind == DaemonReply::Kind::TooOld)
            {
                AdminState::Instance().observeTooOld();
            }

            answer(adminErrorFromReply(reply));
        });
    });
}

void JsonApi::processAdminSet(const Json &request, const AdminAnswer &answerIn)
{
    const AdminAnswer answer = guardedAnswer(answerIn);

    if (!secureTransport())
    {
        answer(adminError("https_required", string()));
        return;
    }

    string newPass;
    const string invalid = passwordFieldError(request, "new_pass", newPass);
    if (!invalid.empty())
    {
        answer(adminError("invalid_request", invalid));
        return;
    }

    //Present but empty still means "change": an empty key is not a bootstrap
    const bool hasSession = request.contains("admin_session");
    const string session = JsonField::string(request, "admin_session");

    withAdminMode([this, answer, newPass, hasSession, session]()
                  {
        const AdminState::Mode mode = AdminState::Instance().mode();

        if (mode != AdminState::Mode::Set && mode != AdminState::Mode::NotSet)
        {
            answer(adminModeRefusal());
            return;
        }

        if (mode == AdminState::Mode::NotSet && hasSession)
        {
            answer(adminNotSet());
            return;
        }

        if (mode == AdminState::Mode::Set && !hasSession)
        {
            answer(adminError("admin_already_set", string()));
            return;
        }

        if (hasSession && !isValidAdminSession(session))
        {
            answer(adminError("bad_session", string()));
            return;
        }

        if (!hasSession && !householdAllowsBootstrap())
        {
            answer(adminError("household_password_required", string()));
            return;
        }

        DaemonClient::Request req;
        req.method = "POST";
        req.path = "admin/set";
        //Built field by field: the daemon's recovery mode (root CLI only) must
        //never be reachable with a field a client sent
        req.body = Json{ { "password", newPass } };
        req.session = session;
        req.clientIp = adminClientIp();

        DaemonClient::Instance().call(req, [answer, hasSession](const DaemonReply &reply)
                                      {
            if (reply.kind == DaemonReply::Kind::Ok)
            {
                AdminState::Instance().observe(true);
                answer(setSucceeded(reply, hasSession));
                return;
            }

            if (reply.kind == DaemonReply::Kind::Error && reply.code == "admin_already_set")
                AdminState::Instance().observe(true);
            else if (reply.kind == DaemonReply::Kind::Error && reply.code == "admin_not_set")
                AdminState::Instance().observe(false);
            else if (reply.kind == DaemonReply::Kind::TooOld)
                AdminState::Instance().observeTooOld();

            answer(adminErrorFromReply(reply));
        });
    });
}

void JsonApi::gateAdminOnly(const Json &request, const AdminAnswer &answerIn, const std::function<void()> &allowedIn)
{
    //Only "no daemon, admin never seen" is served from the cache: an admin set
    //out of band (calaos-os admin set) must close these at once
    if (AdminState::Instance().mode() == AdminState::Mode::Unavailable)
    {
        allowedIn();
        return;
    }

    const AdminAnswer answer = guardedAnswer(answerIn);
    std::weak_ptr<bool> alive = apiAlive;
    const std::function<void()> allowed = [alive, allowedIn]()
    {
        if (!alive.expired())
            allowedIn();
    };

    //The body only: haproxy logs URLs, so a session in the query is never read
    const string session = JsonField::string(request, "admin_session");

    withAdminMode([this, answer, allowed, session]()
                  {
        const AdminState::Mode mode = AdminState::Instance().mode();
        if (mode != AdminState::Mode::Set && mode != AdminState::Mode::Closed)
        {
            allowed();
            return;
        }

        if (!secureTransport())
        {
            answer(adminError("https_required", string()));
            return;
        }

        if (mode == AdminState::Mode::Closed)
        {
            answer(adminError("daemon_unreachable", string()));
            return;
        }

        if (!isValidAdminSession(session))
        {
            answer(adminError("bad_session", string()));
            return;
        }

        validateAdminSession(session, [answer, allowed](const Json &error)
                             {
            if (error.is_null())
                allowed();
            else
                answer(error);
        });
    });
}
