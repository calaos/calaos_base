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
#include "HistLogger.h"
#include "HttpClient.h"
#include "JsonField.h"

#include <openssl/evp.h>

#include <algorithm>
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
        { "package_held", "Package is held on the box (apt-mark hold)" },
        { "approver_required", "No approver phone is enrolled" },
        { "approver_limit", "Two approver phones are already enrolled" },
        { "approver_exists", "This phone is already an approver" },
        { "approver_not_found", "Unknown approver phone" },
        { "approver_last", "The last approver phone cannot be removed" },
        { "code_required", "A push code is required" },
        { "bad_code", "Wrong push code" },
        { "code_expired", "The push code expired, request a new one" },
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

bool isDaemonId(const string &s)
{
    return JsonApi::isDaemonId(s);
}

bool isPushCode(const string &s)
{
    return s.size() == 6 && s.find_first_not_of("0123456789") == string::npos;
}

bool isPasswordValue(const string &s)
{
    return !s.empty() && s.size() <= MaxPasswordBytes;
}

//The push code actions of docs/API.md a client may ask a code for
bool isCodeAction(const string &s)
{
    return s == "console_open" || s == "admin_password" || s == "approver_enroll" || s == "approver_remove";
}

//Empty when from[key] is a string check() accepts, the invalid_request message otherwise
string stringField(const Json &from, const char *key, bool (*check)(const string &), string &out)
{
    const auto it = from.is_object() ? from.find(key) : from.end();
    if (!from.is_object() || it == from.end() || !it->is_string())
        return string(key) + " is required";
    out = it->get<string>();
    if (!check(out))
        return string("Invalid ") + key;
    return string();
}

string codeFields(const Json &from, Json &body)
{
    string id, code;
    string invalid = stringField(from, "code_id", isDaemonId, id);
    if (invalid.empty())
        invalid = stringField(from, "code", isPushCode, code);
    if (!invalid.empty())
        return invalid;
    body["code_id"] = id;
    body["code"] = code;
    return string();
}

//Neither field, or both: the daemon decides whether the action needs a code
string optionalCodeFields(const Json &from, Json &body)
{
    if (!from.is_object() || (!from.contains("code_id") && !from.contains("code")))
        return string();
    return codeFields(from, body);
}

Json paramsOf(const Json &request)
{
    const auto it = request.is_object() ? request.find("params") : request.end();
    return request.is_object() && it != request.end() && it->is_object() ? *it : Json::object();
}

/* The daemon body of a row, built field by field from validated params: empty
 * when it is built, the invalid_request message otherwise. Nothing else the
 * client sent reaches the daemon.
 */
using BodyBuilder = string (*)(const Json &params, Json &body);

string upgradeBody(const Json &params, Json &body)
{
    const auto it = params.find("package");
    if (it == params.end() || !it->is_string())
        return "params.package is required";
    if (!JsonApi::isValidPackageName(it->get<string>()))
        return "Invalid package name";
    body["package"] = it->get<string>();
    return string();
}

string removeBody(const Json &params, Json &body)
{
    string id;
    const string invalid = stringField(params, "approver_id", isDaemonId, id);
    if (!invalid.empty())
        return invalid;
    body["approver_id"] = id;
    return codeFields(params, body);
}

//The token and the device name are added by relayEnroll(), never from the client
string enrollBody(const Json &params, Json &body)
{
    string tokenId;
    const string invalid = stringField(params, "token_id", isDaemonId, tokenId);
    if (!invalid.empty())
        return invalid;
    return optionalCodeFields(params, body);
}

//params is rebuilt with the keys of the Push codes table of docs/API.md only
string codeRequestBody(const Json &params, Json &body)
{
    string action;
    string invalid = stringField(params, "code_action", isCodeAction, action);
    if (!invalid.empty())
        return invalid;

    const auto given = params.find("code_params");
    if (given != params.end() && !given->is_object())
        return "Invalid code_params";
    const Json from = given != params.end() ? *given : Json::object();

    Json bound = Json::object();
    string value;
    if (action == "admin_password")
    {
        invalid = stringField(from, "password", isPasswordValue, value);
        bound["password"] = value;
    }
    else if (action == "approver_remove")
    {
        invalid = stringField(from, "approver_id", isDaemonId, value);
        bound["approver_id"] = value;
    }
    if (!invalid.empty())
        return invalid;

    body["action"] = action;
    body["params"] = bound;
    if (params.contains("approver_id"))
    {
        invalid = stringField(params, "approver_id", isDaemonId, value);
        body["approver_id"] = value;
    }
    return invalid;
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
    BodyBuilder body; //null: no body
};

const RelayRow relayTable[] = {
    { "update_status", "GET", "update/status", false, DaemonClient::DefaultTimeout, nullptr },
    { "update_check", "GET", "update/check", true, DaemonClient::UpdateCheckTimeout, nullptr },
    { "update_available", "GET", "update/available", true, DaemonClient::DefaultTimeout, nullptr },
    { "update_upgrade_all", "POST", "update/upgrade-all", true, DaemonClient::DefaultTimeout, nullptr },
    { "update_upgrade", "POST", "update/upgrade", true, DaemonClient::DefaultTimeout, upgradeBody },
    { "system_reboot", "POST", "system/reboot", true, DaemonClient::DefaultTimeout, nullptr },
    { "system_restart", "POST", "system/restart", true, DaemonClient::RestartTimeout, nullptr },
    { "system_info", "GET", "system/info", true, DaemonClient::DefaultTimeout, nullptr },
    { "approver_list", "GET", "admin/approvers", true, DaemonClient::DefaultTimeout, nullptr },
    //The daemon's list marks which push_tokens entries are approvers already
    { "approver_candidates", "GET", "admin/approvers", true, DaemonClient::DefaultTimeout, nullptr },
    { "approver_enroll", "POST", "admin/approvers/enroll", true, DaemonClient::DefaultTimeout, enrollBody },
    { "approver_confirm", "POST", "admin/approvers/confirm", true, DaemonClient::DefaultTimeout, codeFields },
    { "approver_remove", "POST", "admin/approvers/remove", true, DaemonClient::DefaultTimeout, removeBody },
    { "code_request", "POST", "admin/codes", true, DaemonClient::DefaultTimeout, codeRequestBody },
    { "code_verify", "POST", "admin/codes/verify", true, DaemonClient::DefaultTimeout, codeFields },
    /* The verified code becomes a grant of the session for 30 s: the console
     * WebSocket that carries the same session takes it. When the daemon's
     * console routes name a ticket, it is fetched here, after the verify.
     */
    { "console_open", "POST", "admin/codes/verify", true, DaemonClient::DefaultTimeout, codeFields },
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
                                               "not_allowed", "package_held", "approver_required", "approver_limit",
                                               "approver_exists", "approver_not_found", "approver_last",
                                               "code_required", "bad_code", "code_expired" };

    if (reply.code == "admin_not_set")
        return adminNotSet();

    if (sameCode.count(reply.code))
    {
        Json j = adminError(reply.code, string());
        static const map<string, string> details = { { "rate_limited", "retry_after" },
                                                     { "upgrade_in_progress", "job_id" },
                                                     { "password_too_short", "min_length" },
                                                     { "bad_code", "attempts_left" },
                                                     { "approver_limit", "max" } };
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
    if (action == "mcp_regenerate")
        return Json();

    const RelayRow *row = relayRow(action);
    if (!row)
        return adminError("not_allowed", "Unknown admin action");

    Json body = Json::object();
    const string invalid = row->body ? row->body(paramsOf(request), body) : string();
    if (!invalid.empty())
        return adminError("invalid_request", invalid);

    return Json();
}

long JsonApi::adminRelayTimeout(const string &action)
{
    const RelayRow *row = relayRow(action);
    return row ? row->timeout : 0;
}

namespace
{

//The client answer for a failed relayed call, AdminState told what it says
Json relayFailure(const DaemonReply &reply)
{
    if (reply.kind == DaemonReply::Kind::Error && reply.code == "admin_not_set")
        AdminState::Instance().observe(false);
    else if (reply.kind == DaemonReply::Kind::TooOld)
        AdminState::Instance().observeTooOld();

    return JsonApi::adminErrorFromReply(reply);
}

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

        answer(relayFailure(reply));
    });
}

//Never empty: the daemon refuses an enrolment without a name
string deviceNameOf(const PushToken &t)
{
    const string name = JsonApi::printableText(t.device_name, JsonApi::MaxDeviceName);
    if (!name.empty())
        return name;
    return t.hw_type == HistLogger::PUSH_HW_IOS ? _("iOS device") : _("Android device");
}

//SQLite's DATETIME is UTC
string rfc3339(const string &sqliteTime)
{
    if (sqliteTime.size() != 19 || sqliteTime[10] != ' ')
        return string();
    return sqliteTime.substr(0, 10) + "T" + sqliteTime.substr(11) + "Z";
}

//push_tokens without a single token: the token never leaves this server but
//to the daemon (enrolment) and the push service
void relayCandidates(const DaemonClient::Request &req, const std::function<void(const Json &)> &answer)
{
    DaemonClient::Instance().call(req, [answer](const DaemonReply &reply)
                                  {
        if (reply.kind != DaemonReply::Kind::Ok)
        {
            answer(relayFailure(reply));
            return;
        }

        std::set<string> approvers;
        for (const Json &a: reply.data.is_array() ? reply.data : Json::array())
            approvers.insert(JsonField::string(a, "token_id"));

        HistLogger::Instance().getPushTokens([answer, approvers](bool success, string, const vector<PushToken> &tokensIn)
                                             {
            if (!success)
            {
                answer(JsonApi::adminError("internal", string())); //LCOV_EXCL_LINE history database unavailable
                return;                                            //LCOV_EXCL_LINE history database unavailable
            }

            vector<PushToken> tokens = tokensIn;
            std::stable_sort(tokens.begin(), tokens.end(), [](const PushToken &a, const PushToken &b)
                             {
                return a.created_at > b.created_at;
            });

            Json list = Json::array();
            for (const PushToken &t: tokens)
            {
                const string id = JsonApi::pushTokenId(t.token);
                //The daemon's list is the truth: repairs an alert this server missed
                if (t.approver != (approvers.count(id) > 0))
                    HistLogger::Instance().setPushTokenApprover(t.token, !t.approver);
                list.push_back(Json{ { "token_id", id },
                                     { "device_name", deviceNameOf(t) },
                                     { "last_seen", rfc3339(t.created_at) },
                                     { "approver", approvers.count(id) > 0 } });
            }
            answer(Json{ { "success", "true" }, { "result", list } });
        });
    });
}

//The client names a push_tokens entry; its token and name go to the daemon
void relayEnroll(const DaemonClient::Request &reqIn, const string &tokenId, const std::function<void(const Json &)> &answer)
{
    HistLogger::Instance().getPushTokens([reqIn, tokenId, answer](bool success, string, const vector<PushToken> &tokens)
                                         {
        const auto found = std::find_if(tokens.begin(), tokens.end(), [&tokenId](const PushToken &t)
                                        {
            return JsonApi::pushTokenId(t.token) == tokenId;
        });
        if (found == tokens.end())
        {
            answer(success ? JsonApi::adminError("invalid_request", "Unknown token_id") : JsonApi::adminError("internal", string()));
            return;
        }

        DaemonClient::Request req = reqIn;
        req.body["token"] = found->token;
        req.body["device_name"] = deviceNameOf(*found);
        relayCall(req, false, false, answer);
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
    //Built here from the validated params, whatever else the request holds
    const Json params = paramsOf(request);
    if (row->body)
    {
        req.body = Json::object();
        row->body(params, req.body);
    }

    if (action == "approver_candidates")
        relayCandidates(req, answer);
    else if (action == "approver_enroll")
        relayEnroll(req, JsonField::string(params, "token_id"), answer);
    else
        relayCall(req, household, !row->session, answer);
}

string JsonApi::printableText(const string &in, size_t maxCodePoints)
{
    static const uint32_t minFor[] = { 0, 0, 0x80, 0x800, 0x10000 };
    string out;
    size_t count = 0;
    size_t i = 0;

    while (i < in.size() && count < maxCodePoints)
    {
        const unsigned char c = in[i];
        //Length from the lead byte, 0 for a byte that cannot start a sequence
        const size_t len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                                      : (c & 0xf0) == 0xe0   ? 3
                                      : (c & 0xf8) == 0xf0   ? 4
                                                             : 0;
        uint32_t cp = len == 1 ? c : c & (0x7f >> len);

        bool valid = len > 0 && i + len <= in.size();
        for (size_t k = 1; valid && k < len; k++)
        {
            const unsigned char cc = in[i + k];
            valid = (cc & 0xc0) == 0x80;
            cp = (cp << 6) | (cc & 0x3f);
        }
        //Overlong forms, surrogates and code points beyond Unicode
        valid = valid && cp >= minFor[len] && !(cp >= 0xd800 && cp <= 0xdfff) && cp <= 0x10ffff;
        if (!valid)
        {
            i++;
            continue;
        }

        //Controls, soft hyphen, zero-width, bidi marks, overrides and isolates,
        //line separators, BOM, interlinear annotations, tags, variation
        //selectors, and the fillers and blanks that render as nothing
        const bool hidden = cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) || cp == 0xad || cp == 0x34f || cp == 0x61c ||
                            cp == 0x115f || cp == 0x1160 || cp == 0x180e || cp == 0x2800 || cp == 0x3164 ||
                            cp == 0xffa0 || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
                            (cp >= 0x2060 && cp <= 0x206f) || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff ||
                            (cp >= 0xfff9 && cp <= 0xfffb) || (cp >= 0xe0000 && cp <= 0xe007f) ||
                            (cp >= 0xe0100 && cp <= 0xe01ef);
        //Every Zs space is a plain one, folded and trimmed with the others
        const bool space = cp == ' ' || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x202f ||
                           cp == 0x205f || cp == 0x3000;
        if (space && !out.empty() && out.back() != ' ')
        {
            out.push_back(' ');
            count++;
        }
        else if (!hidden && !space)
        {
            out.append(in, i, len);
            count++;
        }
        i += len;
    }

    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

bool JsonApi::isDaemonId(const string &s)
{
    return s.size() == 16 && s.find_first_not_of("0123456789abcdef") == string::npos;
}

string JsonApi::pushTokenId(const string &token)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(token.data(), token.size(), digest, &len, EVP_sha256(), nullptr) != 1 || len < 8)
        return string(); //LCOV_EXCL_LINE only reached when OpenSSL has no SHA-256

    static const char *hex = "0123456789abcdef";
    string out;
    for (unsigned int i = 0; i < 8; i++)
    {
        out.push_back(hex[digest[i] >> 4]);
        out.push_back(hex[digest[i] & 0x0f]);
    }
    return out;
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
    const string key = loginThrottleKey("admin:");
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

    //The push code of the admin_password action: a change only reads it
    Json code = Json::object();
    const string invalidCode = optionalCodeFields(request, code);

    withAdminMode([this, answer, newPass, code, invalidCode, hasSession, session]()
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

        if (hasSession && !invalidCode.empty())
        {
            answer(adminError("invalid_request", invalidCode));
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
        if (hasSession)
            req.body.update(code);
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
