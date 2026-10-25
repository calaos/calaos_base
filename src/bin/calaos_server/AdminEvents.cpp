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
#include "AdminEvents.h"
#include "AdminState.h"
#include "DaemonClient.h"
#include "HistLogger.h"
#include "JsonApi.h"
#include "JsonField.h"
#include "NotifManager.h"
#include "Timer.h"
#include "WebSocketFrame.h"
#include "libuvw.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>

namespace Calaos
{

namespace
{

string utcNow()
{
    const time_t t = ::time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

//A short machine token from the daemon (status, step, kind): anything else is
//dropped rather than relayed
bool isToken(const Json &v, size_t max)
{
    if (!v.is_string())
        return false;
    const string &s = v.get_ref<const string &>();
    if (s.empty() || s.size() > max)
        return false;
    for (char c: s)
    {
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != ':' && c != '.' && c != '+')
            return false;
    }
    return true;
}

bool isScalar(const Json &v)
{
    return v.is_number() || v.is_boolean() || isToken(v, 64);
}

void copyScalars(const Json &from, Json &to, std::initializer_list<const char *> keys)
{
    for (const char *k: keys)
    {
        const auto it = from.find(k);
        if (it != from.end() && isScalar(*it))
            to[k] = *it;
    }
}

bool isAlertType(const string &type)
{
    return type == "admin_set" || type == "admin_reset" || type == "approver_set" || type == "approver_removed" ||
           type == "console_opened" || type == "network_rolled_back";
}

//The daemon's rule for a push token: 1 to 4096 printable ASCII characters, no space
bool isPushToken(const string &token)
{
    if (token.empty() || token.size() > 4096)
        return false;
    return std::all_of(token.begin(), token.end(), [](char c)
                       {
        return c > ' ' && c < 0x7f;
    });
}

//The daemon's text, "Calaos admin code NNNNNN: <what it allows>": anything
//else is not relayed, so a squatter of its port cannot word its own pushes
bool isCodeMessage(const string &text)
{
    static const string prefix = "Calaos admin code ";
    const size_t digits = prefix.size();
    if (text.size() < digits + 8 || text.compare(0, digits, prefix) != 0 || text.compare(digits + 6, 2, ": ") != 0)
        return false;
    return std::all_of(text.begin() + digits, text.begin() + digits + 6, [](char c)
                       {
        return c >= '0' && c <= '9';
    });
}

bool isAlertId(const string &id)
{
    if (id.empty() || id.size() > 64)
        return false;
    for (char c: id)
    {
        if (!isxdigit((unsigned char)c))
            return false;
    }
    return true;
}

//http://127.0.0.1[:port] or http://[::1][:port], as DaemonClient accepted it
bool splitBase(const string &base, string &host, int &port, bool &v6)
{
    static const string scheme = "http://";
    if (base.compare(0, scheme.size(), scheme) != 0)
        return false;
    string rest = base.substr(scheme.size());
    string after;
    v6 = !rest.empty() && rest[0] == '[';
    if (v6)
    {
        //DaemonClient::parseBaseUrl() already checked the closing bracket
        const string::size_type end = std::min(rest.find(']'), rest.size());
        host = rest.substr(1, end - 1);
        after = rest.substr(std::min(end + 1, rest.size()));
    }
    else
    {
        const string::size_type colon = rest.find(':');
        host = rest.substr(0, colon);
        if (colon != string::npos)
            after = rest.substr(colon);
    }
    port = after.size() > 1 ? std::atoi(after.c_str() + 1) : 80;
    return !host.empty() && port > 0;
}

} //namespace

AdminEvents &AdminEvents::Instance()
{
    static AdminEvents inst;
    return inst;
}

AdminEvents::AdminEvents()
{
}

double AdminEvents::nextBackoff(double current, const Timings &t)
{
    if (current <= 0.0)
        return t.minBackoff;
    return std::min(current * 2.0, t.maxBackoff);
}

Json AdminEvents::householdStatus(const Json &statusIn)
{
    Json out = Json::object();
    const Json status = statusIn.is_object() ? statusIn : Json::object();

    copyScalars(status, out, { "status", "total_percent", "current_step", "current_percent", "pkg_index", "pkg_count", "progress", "progress_total" });

    //The daemon's summary may name the failed packages
    const auto err = status.find("error");
    if (err != status.end() && err->is_string() && !err->get_ref<const string &>().empty())
        out["error"] = "upgrade failed";

    const auto packages = status.find("packages");
    if (packages != status.end() && packages->is_array())
    {
        Json list = Json::array();
        for (const Json &p: *packages)
        {
            Json entry = Json::object();
            if (p.is_object())
                copyScalars(p, entry, { "state", "percent" });
            list.push_back(entry);
        }
        out["packages"] = list;
    }

    return out;
}

Json AdminEvents::householdJob(const Json &job)
{
    if (!job.is_object())
        return Json();

    Json out = Json::object();
    copyScalars(job, out, { "id", "kind", "status", "reboot_required", "daemon_restart", "started_at", "finished_at" });

    const auto err = job.find("error");
    if (err != job.end() && err->is_string() && !err->get_ref<const string &>().empty())
        out["error"] = err->get_ref<const string &>() == "interrupted" ? "interrupted" : "upgrade failed";

    return out;
}

Json AdminEvents::householdEventFor(const Json &event, const string &receivedAt)
{
    const string type = JsonField::string(event, "type");
    const Json data = event.is_object() && event.contains("data") ? event["data"] : Json();

    if (type == "upgrade_progress")
    {
        if (!data.is_object())
            return Json();
        return Json{ { "type", type }, { "ts", receivedAt }, { "data", householdStatus(data) } };
    }

    const string intf = JsonField::string(data, "intf");
    const bool knownIntf = JsonApi::isValidInterfaceName(intf);

    if (isAlertType(type))
    {
        const auto ts = data.is_object() ? data.find("ts") : data.end();
        const bool usable = data.is_object() && ts != data.end() && isToken(*ts, 40);
        Json out = { { "type", type }, { "ts", usable ? ts->get<string>() : receivedAt } };
        if (type == "network_rolled_back")
            out["data"] = knownIntf ? Json{ { "intf", intf } } : Json::object();
        return out;
    }

    //No address: these go to the plain port too
    if (type == "network_pending" || type == "network_applied")
    {
        if (!knownIntf || !JsonApi::isDaemonId(JsonField::string(data, "change_id")))
            return Json();
        Json out = { { "intf", intf }, { "change_id", data["change_id"] } };
        if (type == "network_pending")
        {
            const auto applied = data.find("applied_at");
            const auto within = data.find("confirm_within");
            if (applied == data.end() || !isToken(*applied, 40) || within == data.end() || !within->is_number_integer())
                return Json();
            out["applied_at"] = *applied;
            out["confirm_within"] = *within;
        }
        return Json{ { "type", type }, { "ts", receivedAt }, { "data", out } };
    }

    return Json();
}

void AdminEvents::start()
{
    if (state != State::Stopped)
        return;
    state = State::Waiting;
    backoff = 0.0;
    connect();
}

void AdminEvents::stop()
{
    state = State::Stopped;
    generation++;
    if (handle && !handle->closing())
        handle->close();
    handle.reset();
    delete watchdogTimer;
    watchdogTimer = nullptr;
    delete reconnectTimer;
    reconnectTimer = nullptr;
    for (auto &r: retries)
        delete r.second.timer;
    retries.clear();
}

void AdminEvents::scheduleReconnect(double delay)
{
    delete reconnectTimer;
    //A never destroyed singleton: `this` cannot dangle. The timer deletes
    //itself from its own callback, which Timer allows.
    reconnectTimer = new Timer(delay, sigc::mem_fun(*this, &AdminEvents::reconnectNow));
}

void AdminEvents::reconnectNow()
{
    delete reconnectTimer;
    reconnectTimer = nullptr;
    if (state == State::Waiting)
        connect();
}

void AdminEvents::connect()
{
    string host;
    int port = 0;
    bool v6 = false;
    if (!splitBase(DaemonClient::Instance().baseUrl(), host, port, v6))
    {
        //Calls to the daemon are disabled: look again later, the cost is a timer
        state = State::Waiting;
        scheduleReconnect(timings.maxBackoff);
        return;
    }

    const unsigned long gen = ++generation;
    state = State::Connecting;
    lastActivity = Utils::getMainLoopTime();
    inbuf.clear();
    message.clear();
    inMessage = false;
    frame.reset(new WebSocketFrame());

    key = DaemonClient::webSocketKey();

    handle = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();

    handle->once<uvw::ConnectEvent>([this, gen, host, port](const auto &, uvw::TcpHandle &h)
                                    {
        if (gen == generation)
            onConnected(h, host, port);
    });

    handle->on<uvw::ErrorEvent>([this, gen](const uvw::ErrorEvent &ev, uvw::TcpHandle &)
                                {
        if (gen == generation)
            drop(ev.what());
    });

    handle->once<uvw::EndEvent>([this, gen](const auto &, uvw::TcpHandle &)
                                {
        if (gen == generation)
            drop("closed by calaos-container");
    });

    handle->on<uvw::DataEvent>([this, gen](const uvw::DataEvent &ev, uvw::TcpHandle &)
                               {
        if (gen == generation)
            onData(ev.data.get(), ev.length);
    });

    delete watchdogTimer;
    watchdogTimer = new Timer(timings.ping, sigc::mem_fun(*this, &AdminEvents::watchdog));

    v6 ? handle->connect<uvw::IPv6>(host, port) : handle->connect(host, port);
}

void AdminEvents::onConnected(uvw::TcpHandle &h, const string &host, int port)
{
    h.read();

    std::ostringstream req;
    req << "GET /api/v2/events HTTP/1.1\r\n"
        << "Host: " << (host.find(':') != string::npos ? "[" + host + "]" : host) << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << key << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n";
    //The header only: the daemon refuses ?token= on /api/v2/events
    if (!DaemonClient::Instance().bearer().empty())
        req << "Authorization: Bearer " << DaemonClient::Instance().bearer() << "\r\n";
    req << "\r\n";
    write(req.str());
}

void AdminEvents::drop(const string &why, double delay)
{
    const bool wasOpen = state == State::Open;

    generation++;
    if (handle && !handle->closing())
        handle->close();
    handle.reset();
    delete watchdogTimer;
    watchdogTimer = nullptr;

    //Only the callbacks of a live connection call this, and stop() made
    //every one of them stale. One line per outage: a flapping daemon would
    //otherwise log at every reconnect.
    if (!failureLogged)
    {
        cInfoDom("admin") << (wasOpen ? "Lost the calaos-container event stream (" : "calaos-container event stream unavailable (")
                          << why << "), retrying in the background";
        failureLogged = true;
    }
    else
    {
        cDebugDom("admin") << "calaos-container event stream: " << why;
    }

    state = State::Waiting;
    if (delay < 0.0)
    {
        backoff = nextBackoff(backoff, timings);
        delay = backoff;
    }
    scheduleReconnect(delay);
}

void AdminEvents::watchdog()
{
    const double now = Utils::getMainLoopTime();
    if (now - lastActivity > timings.silence)
    {
        drop(state == State::Open ? "no answer to the pings" : "no handshake answer");
        return;
    }

    //Only a stream that stayed up resets the backoff: a daemon that accepts
    //and closes at once keeps being retried slower and slower
    if (state == State::Open && now - connectedAt >= timings.silence && (backoff > 0.0 || failureLogged))
    {
        if (failureLogged)
            cInfoDom("admin") << "Subscribed to the calaos-container event stream again";
        backoff = 0.0;
        failureLogged = false;
    }

    if (state == State::Open)
        sendFrame(WebSocketFrame::OpCodePing, string());
}

void AdminEvents::onData(const char *data, size_t len)
{
    const unsigned long gen = generation;
    lastActivity = Utils::getMainLoopTime();
    inbuf.append(data, len);

    if (state == State::Connecting && !processHandshake())
        return;

    if (gen == generation && state == State::Open)
        processFrames();
}

bool AdminEvents::processHandshake()
{
    const string::size_type end = inbuf.find("\r\n\r\n");
    if (end == string::npos)
    {
        if (inbuf.size() > MaxHandshakeSize)
            drop("handshake too long");
        return false;
    }

    const string head = inbuf.substr(0, end);
    inbuf.erase(0, end + 4);

    map<string, string> headers;
    const int status = DaemonClient::parseHttpHead(head, headers);

    if (status == 401)
    {
        //Same rule as DaemonClient: one token re-read, then the refusal is logged once
        if (!tokenRetried)
        {
            tokenRetried = true;
            DaemonClient::Instance().readToken();
            drop("bearer refused, token read again", 0.0);
        }
        else
        {
            DaemonClient::Instance().bearerRefused();
            drop("bearer refused");
        }
        return false;
    }

    if (status == 404)
    {
        if (!tooOldLogged)
            cInfoDom("admin") << "calaos-container has no /api/v2/events yet (old daemon in memory)";
        tooOldLogged = true;
        drop("old daemon", timings.maxBackoff);
        return false;
    }

    if (status != 101 || Utils::str_to_lower(headers["upgrade"]) != "websocket" ||
        headers["sec-websocket-accept"] != DaemonClient::webSocketAccept(key))
    {
        drop("handshake refused with HTTP " + Utils::to_string(status));
        return false;
    }

    tokenRetried = false;
    tooOldLogged = false;
    DaemonClient::Instance().bearerAccepted();
    state = State::Open;
    connectedAt = Utils::getMainLoopTime();
    cDebugDom("admin") << "Subscribed to the calaos-container event stream";
    return true;
}

void AdminEvents::processFrames()
{
    const unsigned long gen = generation;

    while (!inbuf.empty() && gen == generation)
    {
        if (!frame->processFrameData(inbuf))
            return;

        //A zero length frame is flagged as an error by the parser yet stays
        //valid: the daemon's pings carry no payload
        if (!frame->isValid() || frame->hasMask())
        {
            drop("protocol error");
            return;
        }

        const int opcode = frame->getOpcode();
        const string payload = frame->hasError() ? string() : frame->getPayload();
        const bool final = frame->isFinalFrame();
        frame->clear();

        switch (opcode)
        {
        case WebSocketFrame::OpCodePing:
            sendFrame(WebSocketFrame::OpCodePong, payload);
            break;
        case WebSocketFrame::OpCodePong:
            break;
        case WebSocketFrame::OpCodeClose:
            sendFrame(WebSocketFrame::OpCodeClose, payload.substr(0, 2));
            drop("closed by calaos-container");
            return;
        case WebSocketFrame::OpCodeContinue:
        case WebSocketFrame::OpCodeText:
        case WebSocketFrame::OpCodeBinary:
        {
            if ((opcode == WebSocketFrame::OpCodeContinue) != inMessage)
            {
                drop("protocol error");
                return;
            }
            if (opcode != WebSocketFrame::OpCodeContinue)
            {
                message.clear();
                binaryMessage = opcode == WebSocketFrame::OpCodeBinary;
            }
            message += payload;
            inMessage = !final;
            if (message.size() > MaxMessageSize)
            {
                drop("message too big");
                return;
            }
            if (final && !binaryMessage)
                processMessage(message);
            break;
        }
        }
    }
}

void AdminEvents::processMessage(const string &text)
{
    //nlohmann recurses to the depth of the document: a deep one would take
    //the whole server down, and the reconnect would read it again
    if (!JsonApi::requestNestingWithinLimit(text))
    {
        cWarningDom("admin") << "calaos-container event nested too deep, dropped";
        return;
    }

    const Json event = Json::parse(text, nullptr, false);
    const string type = JsonField::string(event, "type");

    if (isAlertType(type))
    {
        processAlert(type, event.contains("data") ? event["data"] : Json());
        return;
    }

    if (type == "push_code")
    {
        processPushCode(event.contains("data") ? event["data"] : Json());
        return;
    }

    const Json out = householdEventFor(event, utcNow());
    if (out.is_null())
    {
        cDebugDom("admin") << "calaos-container event not relayed: " << (type.empty() ? string("invalid") : type);
        return;
    }

    householdEvent.emit(out);
}

void AdminEvents::processAlert(const string &type, const Json &data)
{
    const string id = JsonField::string(data, "id");
    if (!isAlertId(id))
    {
        //Acked all the same, or the daemon would replay it for ever; never
        //stored nor relayed
        if (!id.empty() && id.size() <= MaxForeignAlertId)
            ack(id);
        if (unusableAlerts.size() < MaxRemembered && unusableAlerts.insert(id).second)
            cWarningDom("admin") << "calaos-container sent a " << type << " alert without a usable id";
        return;
    }

    if (type == "approver_set" || type == "approver_removed")
        markApprover(JsonField::string(data, "token_id"), type == "approver_set");

    //Its ack waits for the event of the copy already being stored
    if (storing.count(id))
        return;

    storing.insert(id);
    //Acked once stored, whatever becomes of the push: an alert left pending
    //would be replayed, and stored again, at every restart
    auto stored = [this, id]()
    {
        storing.erase(id);
        ack(id);
    };
    const bool fresh = AdminState::Instance().storeAlert(type, id, stored, data);

    if (!fresh)
    {
        //Stored before, maybe before a restart: an ack lost earlier is retried here
        storing.erase(id);
        ack(id);
        return;
    }

    cInfoDom("admin") << "calaos-container alert: " << type;
    householdEvent.emit(householdEventFor(Json{ { "type", type }, { "data", data } }, utcNow()));
    if (type == "admin_set" || type == "admin_reset")
        AdminState::Instance().refresh();
    //The banner at the next login says it; no phone is woken for it
    if (type != "network_rolled_back")
        push(type, id, data);
}

void AdminEvents::markApprover(const string &tokenId, bool approver)
{
    HistLogger::Instance().getPushTokens([tokenId, approver](bool, string, const vector<PushToken> &tokens)
                                         {
        for (const PushToken &t: tokens)
        {
            if (t.approver != approver && JsonApi::pushTokenId(t.token) == tokenId)
                HistLogger::Instance().setPushTokenApprover(t.token, approver);
        }
    });
}

void AdminEvents::processPushCode(const Json &data)
{
    //Never relayed: the event only says which code to fetch
    const string codeId = JsonField::string(data, "code_id");
    if (!JsonApi::isDaemonId(codeId))
    {
        cWarningDom("admin") << "calaos-container sent a push_code without a usable code_id";
        return;
    }

    const double now = Utils::getMainLoopTime();
    auto expired = [now](double t)
    {
        return now - t >= 3600.0;
    };
    codePushes.erase(std::remove_if(codePushes.begin(), codePushes.end(), expired), codePushes.end());
    if (codePushes.size() >= timings.maxCodePushes)
    {
        cWarningDom("admin") << "Push code " << codeId << " not fetched: " << timings.maxCodePushes << " sent in the last hour";
        return;
    }

    DaemonClient::Request req;
    req.path = "admin/push/" + codeId;
    DaemonClient::Instance().call(req, [this, codeId](const DaemonReply &reply)
                                  {
        const string token = JsonField::string(reply.data, "token");
        const string text = JsonApi::printableText(JsonField::string(reply.data, "message"), MaxCodeMessage);
        if (reply.kind != DaemonReply::Kind::Ok || !isPushToken(token) || !isCodeMessage(text))
        {
            cWarningDom("admin") << "calaos-container did not hand push code " << codeId << " over (HTTP " << reply.status << ")";
            return;
        }

        //The token and the message hold the second factor: neither is logged
        NotifManager::Instance().sendPushNotificationTo(token, text, [this, codeId](bool sent)
                                                        {
            //Only a push that went out counts: fetches that serve nothing, or
            //name no registered phone, as a squatter's, must not use up the
            //hour of the real daemon
            if (sent)
            {
                codePushes.push_back(Utils::getMainLoopTime());
                cInfoDom("admin") << "Push code " << codeId << " sent to its approver phone";
            }
            else
            {
                cWarningDom("admin") << "Push code " << codeId << " could not be sent";
            }
        });
    });
}

void AdminEvents::push(const string &type, const string &id, const Json &data)
{
    auto done = [this, type, id, data](bool sent)
    {
        if (sent)
            forgetRetry(RetryKind::Push, id);
        else
            scheduleRetry(RetryKind::Push, type, id, data);
    };
    AdminState::Instance().pushAlert(type, done, data);
}

void AdminEvents::ack(const string &id)
{
    if (!acking.insert(id).second)
        return;

    DaemonClient::Request req;
    req.method = "POST";
    req.path = "admin/alerts/ack";
    req.body = Json{ { "ids", Json::array({ id }) } };

    DaemonClient::Instance().call(req, [this, id](const DaemonReply &reply)
                                  {
        acking.erase(id);
        if (reply.kind == DaemonReply::Kind::Ok)
        {
            forgetRetry(RetryKind::Ack, id);
            return;
        }
        cWarningDom("admin") << "calaos-container did not take the ack of alert " << id << ", retrying later";
        scheduleRetry(RetryKind::Ack, string(), id, Json());
    });
}

void AdminEvents::scheduleRetry(RetryKind kind, const string &type, const string &id, const Json &data)
{
    /* The daemon never resends an alert on the stream it already went out
     * on, and an acked one never: a lost push or ack is retried here, a few
     * times. A push or an ack answered after stop() schedules nothing. */
    if (state == State::Stopped)
        return;

    const string slot = string(kind == RetryKind::Push ? "push:" : "ack:") + id;
    auto it = retries.find(slot);
    if (it == retries.end())
    {
        //A series that gives up frees its slot: a full table holds only live series
        if (retries.size() >= MaxRemembered)
            return;
        it = retries.emplace(slot, Retry()).first;
    }

    Retry &r = it->second;
    if (r.timer)
        return;
    if (r.attempts >= timings.maxRetries)
    {
        cInfoDom("admin") << "Giving up the " << (kind == RetryKind::Push ? "push" : "ack") << " of alert " << id;
        retries.erase(it);
        return;
    }

    const double delay = timings.retryDelay * double(1 << r.attempts);
    r.attempts++;
    //A never destroyed singleton: `this` cannot dangle; the timer is deleted
    //from its own callback, which Timer allows
    r.timer = new Timer(delay, [this, kind, type, id, data]()
                        {
        retryNow(kind, type, id, data);
    });
}

void AdminEvents::rearmRetries(double delay)
{
    for (auto &r: retries)
        if (r.second.timer)
            r.second.timer->Reset(delay);
}

//By value: deleting the timer below destroys the lambda that holds the originals
void AdminEvents::retryNow(RetryKind kind, string type, string id, Json data)
{
    auto it = retries.find(string(kind == RetryKind::Push ? "push:" : "ack:") + id);
    if (it == retries.end())
        return; //LCOV_EXCL_LINE stop() deletes the timers with their entries
    delete it->second.timer;
    it->second.timer = nullptr;

    if (kind == RetryKind::Push)
        push(type, id, data);
    else
        ack(id);
}

void AdminEvents::forgetRetry(RetryKind kind, const string &id)
{
    auto it = retries.find(string(kind == RetryKind::Push ? "push:" : "ack:") + id);
    if (it == retries.end())
        return;
    delete it->second.timer;
    retries.erase(it);
}

void AdminEvents::sendFrame(int opcode, const string &payload)
{
    write(WebSocketFrame::makeFrame(opcode, payload, true, DaemonClient::webSocketMask()));
}

void AdminEvents::write(const string &bytes)
{
    auto buf = std::unique_ptr<char[]>(new char[bytes.size()]);
    std::copy(bytes.begin(), bytes.end(), buf.get());
    if (handle && !handle->closing())
        handle->write(std::move(buf), (unsigned int)bytes.size());
}

} //namespace Calaos
