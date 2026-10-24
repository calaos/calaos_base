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
#include "AdminState.h"
#include "EventManager.h"
#include "HistLogger.h"
#include "JsonApi.h"
#include "JsonField.h"
#include "NotifManager.h"
#include "Timer.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unistd.h>

namespace Calaos
{

AdminState &AdminState::Instance()
{
    static AdminState inst;
    return inst;
}

AdminState::AdminState()
{
}

string AdminState::flagPath() const
{
    return Utils::getCachePath() + "/" + FlagFileName;
}

void AdminState::loadFlag()
{
    seenSet = ::access(flagPath().c_str(), F_OK) == 0;
}

void AdminState::setFlag(bool present)
{
    if (present == seenSet)
        return;
    seenSet = present;

    if (present)
    {
        std::ofstream f(flagPath(), std::ios::out | std::ios::trunc);
        f << "1\n";
        if (!f)
            cErrorDom("admin") << "Cannot write " << flagPath()
                               << ", the admin rules will not survive a restart without calaos-container";
    }
    else
    {
        ::unlink(flagPath().c_str());
    }
}

void AdminState::start()
{
    loadFlag();
    loadAlerts();
    started = true;
    startTime = Utils::getMainLoopTime();
    refresh();
}

void AdminState::stop()
{
    started = false;
    delete pollTimer;
    pollTimer = nullptr;
}

AdminState::Mode AdminState::mode() const
{
    switch (reach)
    {
    case Reach::Set: return Mode::Set;
    case Reach::NotSet: return Mode::NotSet;
    case Reach::TooOld: return Mode::TooOld;
    case Reach::Unknown: break;
    }

    return seenSet ? Mode::Closed : Mode::Unavailable;
}

bool AdminState::enforced() const
{
    const Mode m = mode();
    return m == Mode::Set || m == Mode::Closed;
}

void AdminState::observe(bool adminSet)
{
    reach = adminSet ? Reach::Set : Reach::NotSet;
    setFlag(adminSet);
}

void AdminState::observeTooOld()
{
    reach = Reach::TooOld;
    setFlag(false);
}

void AdminState::observeUnreachable()
{
    reach = Reach::Unknown;
}

double AdminState::pollDelay(double sinceStart, bool known)
{
    return (!known && sinceStart < FastPollWindow) ? FastPollInterval : SlowPollInterval;
}

void AdminState::refresh(std::function<void()> done)
{
    if (done)
        waiting.push_back(done);

    if (polling)
        return;
    polling = true;

    DaemonClient::Request req;
    req.path = "admin/status";
    DaemonClient::Instance().call(req, [this](const DaemonReply &reply)
                                  {
        pollDone(reply);
    });
}

void AdminState::pollDone(const DaemonReply &reply)
{
    if (reply.kind == DaemonReply::Kind::Ok && reply.data.is_object() &&
        reply.data.contains("admin_set") && reply.data["admin_set"].is_boolean())
        observe(reply.data["admin_set"].get<bool>());
    else if (reply.kind == DaemonReply::Kind::TooOld)
        observeTooOld();
    else
        observeUnreachable();

    if (reach == Reach::Unknown && !failureLogged)
    {
        cWarningDom("admin") << "calaos-container did not answer admin/status"
                             << (seenSet ? ", admin actions are closed until it does" : ", administration is unavailable");
        failureLogged = true;
    }
    else if (reach != Reach::Unknown && failureLogged)
    {
        cInfoDom("admin") << "calaos-container answers admin/status again";
        failureLogged = false;
    }

    polling = false;

    std::vector<std::function<void()>> callbacks;
    callbacks.swap(waiting);
    for (auto &cb: callbacks)
        cb();

    if (started)
        scheduleNextPoll();
}

void AdminState::pollNow()
{
    refresh();
}

void AdminState::scheduleNextPoll()
{
    const double delay = pollDelay(Utils::getMainLoopTime() - startTime, reach != Reach::Unknown);

    //A never destroyed singleton, `this` cannot dangle
    if (!pollTimer)
        pollTimer = new Timer(delay, sigc::mem_fun(*this, &AdminState::pollNow));
    else
        pollTimer->Reset(delay);
}

void AdminState::resetAlertPushes(double interval)
{
    alertPushInterval = interval;
    lastAlertPush.clear();
}

string AdminState::alertsPath() const
{
    return Utils::getCachePath() + "/" + AlertsFileName;
}

void AdminState::loadAlerts()
{
    storedAlerts.clear();
    std::ifstream f(alertsPath());
    string line;
    while (std::getline(f, line))
    {
        line = Utils::trim(line);
        if (!line.empty() && line.size() <= 64 && line.find_first_not_of("0123456789abcdefABCDEF") == string::npos)
            storedAlerts.push_back(line);
    }
    while (storedAlerts.size() > MaxStoredAlerts)
        storedAlerts.pop_front();
}

void AdminState::saveAlerts()
{
    //Replaced whole: a power cut leaves the old list or the new one
    const string tmp = alertsPath() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::out | std::ios::trunc);
        for (const string &id: storedAlerts)
            f << id << "\n";
        f.flush();
        if (!f)
        {
            cErrorDom("admin") << "Cannot write " << tmp << ", an alert replayed after a restart is stored again";
            return;
        }
    }
    if (::rename(tmp.c_str(), alertsPath().c_str()) != 0)
        cErrorDom("admin") << "Cannot write " << alertsPath() << ", an alert replayed after a restart is stored again"; //LCOV_EXCL_LINE rename in a directory just written to
}

string AdminState::alertMessage(const string &type, const Json &data)
{
    if (type == "admin_set")
        return _("Admin password set on your Calaos box");
    if (type == "admin_reset")
        return _("Admin password removed from your Calaos box");
    //Fixed: the alert's session_audit_id stays in the daemon's audit log
    if (type == "console_opened")
        return _("Calaos: console opened");
    if (type != "approver_set" && type != "approver_removed")
        return string();

    //No quote of the name may close the one around it
    string name = JsonApi::printableText(JsonField::string(data, "device_name"), JsonApi::MaxDeviceName);
    for (const char *quote: { "\"", "\u00ab", "\u00bb", "\u201c", "\u201d", "\u201e", "\u201f", "\u2033", "\u2036", "\uff02" })
        Utils::replace_str(name, quote, "'");
    if (name.empty())
        name = "?";
    const string prefix = type == "approver_set" ? _("New approver phone on your Calaos box: ")
                                                 : _("Approver phone removed from your Calaos box: ");
    return prefix + "\"" + name + "\"";
}

bool AdminState::storeAlert(const string &type, const string &id, std::function<void()> stored, const Json &data)
{
    const string message = alertMessage(type, data);
    if (message.empty())
        return false;

    if (std::find(storedAlerts.begin(), storedAlerts.end(), id) != storedAlerts.end())
        return false;

    storedAlerts.push_back(id);
    while (storedAlerts.size() > MaxStoredAlerts)
        storedAlerts.pop_front();
    saveAlerts();

    HistEvent e = HistEvent::create();
    e.event_type = CalaosEvent::EventPushNotification;
    e.event_raw = Json{ { "message", message }, { "pic_uid", "" } }.dump(-1, ' ', true, Json::error_handler_t::replace);
    HistLogger::Instance().appendEvent(e);

    //HistLogger's worker runs in order: once this read is answered, the
    //event above is written
    HistLogger::Instance().getPushTokens([stored](bool, string, const vector<PushToken> &)
                                         {
        if (stored)
            stored();
    });

    return true;
}

void AdminState::pushAlert(const string &type, std::function<void(bool)> done, const Json &data)
{
    const string message = alertMessage(type, data);

    //Each approver change is its own alert: a decoy phone enrolled first must
    //not silence the push of the next one. Every console opening is pushed: one
    //opened right after the admin's own must still reach the phones.
    const bool approver = type == "approver_set" || type == "approver_removed";
    const string key = type == "console_opened" ? type + ":" + JsonField::string(data, "id")
                       : approver               ? type + ":" + JsonField::string(data, "token_id")
                                                : type;

    const double now = Utils::getMainLoopTime();
    const auto last = lastAlertPush.find(key);
    if (last != lastAlertPush.end() && now - last->second < alertPushInterval)
    {
        cInfoDom("admin") << "Alert " << type << " stored without a push, one was sent less than "
                          << (long)alertPushInterval << " s ago";
        if (done)
            done(true);
        return;
    }

    //No push yet reads as one infinitely long ago
    const double previous = last != lastAlertPush.end() ? last->second : -std::numeric_limits<double>::infinity();
    lastAlertPush[key] = now;

    //A never destroyed singleton: `this` cannot dangle
    NotifManager::Instance().sendPushNotification(message, string(), {}, [this, key, done, previous, now](bool sent)
                                                  {
        if (!sent)
        {
            //Unless a newer push of the type went out meanwhile
            const auto stamp = lastAlertPush.find(key);
            if (stamp != lastAlertPush.end() && stamp->second == now)
                stamp->second = previous;
        }
        if (done)
            done(sent);
    });
}

} //namespace Calaos
