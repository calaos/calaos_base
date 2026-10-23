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
#include "NotifManager.h"
#include "Timer.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <unistd.h>

namespace Calaos
{

namespace
{

//The daemon keeps at most 32 unacknowledged alerts
constexpr size_t MaxRecordedAlerts = 64;

} //namespace

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

namespace
{

bool remembered(const std::deque<string> &ids, const string &id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void remember(std::deque<string> &ids, const string &id)
{
    ids.push_back(id);
    if (ids.size() > MaxRecordedAlerts)
        ids.pop_front();
}

} //namespace

void AdminState::resetAlertPushes(double interval)
{
    alertPushInterval = interval;
    lastAlertPush.clear();
}

bool AdminState::recordAlert(const string &type, const string &id, std::function<void(bool)> done)
{
    string message;
    if (type == "admin_set")
        message = _("Admin password set on your Calaos box");
    else if (type == "admin_reset")
        message = _("Admin password removed from your Calaos box");
    else
        return false;

    if (remembered(recordedAlerts, id))
        return false;
    remember(recordedAlerts, id);

    if (!remembered(storedAlerts, id))
    {
        remember(storedAlerts, id);
        HistEvent e = HistEvent::create();
        e.event_type = CalaosEvent::EventPushNotification;
        e.event_raw = Json{ { "message", message }, { "pic_uid", "" } }.dump(-1, ' ', true, Json::error_handler_t::replace);
        HistLogger::Instance().appendEvent(e);
    }

    const double now = Utils::getMainLoopTime();
    const auto last = lastAlertPush.find(type);
    if (last != lastAlertPush.end() && now - last->second < alertPushInterval)
    {
        cInfoDom("admin") << "Alert " << type << " stored without a push, one was sent less than "
                          << (long)alertPushInterval << " s ago";
        if (done)
            done(true);
        return true;
    }

    //No push yet reads as one infinitely long ago
    const double previous = last != lastAlertPush.end() ? last->second : -std::numeric_limits<double>::infinity();
    lastAlertPush[type] = now;

    //HistLogger's worker runs in order: the event above is stored before the
    //tokens are read, so a successful push means both are done.
    //A never destroyed singleton: `this` cannot dangle.
    NotifManager::Instance().sendPushNotification(message, string(), {}, [this, type, id, done, previous](bool sent)
                                                  {
        if (!sent)
        {
            recordedAlerts.erase(std::remove(recordedAlerts.begin(), recordedAlerts.end(), id), recordedAlerts.end());
            lastAlertPush[type] = previous;
        }
        if (done)
            done(sent);
    });

    return true;
}

} //namespace Calaos
