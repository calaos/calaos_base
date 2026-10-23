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
#ifndef S_AdminState_H
#define S_AdminState_H

#include "Calaos.h"
#include "DaemonClient.h"

#include <deque>
#include <functional>

class Timer;

namespace Calaos
{

/* Whether an admin password is defined, as calaos-container last said, with
 * the sticky rule of docs/08_http_api.md: once an admin has been seen set, a
 * daemon that stops answering keeps the server closed instead of reopening it.
 */
class AdminState
{
public:
    enum class Mode
    {
        Set,         //daemon reachable, admin defined
        NotSet,      //daemon reachable, no admin
        TooOld,      //the daemon in memory has no admin routes
        Closed,      //daemon unreachable, admin seen set before: fail closed
        Unavailable, //daemon unreachable, admin never seen set: as without admin
    };

    static constexpr double FastPollInterval = 5.0;
    static constexpr double SlowPollInterval = 60.0;
    static constexpr double FastPollWindow = 120.0;
    static constexpr const char *FlagFileName = "admin_seen_set";
    //The ids of the alerts stored, one per line, the newest last
    static constexpr const char *AlertsFileName = "admin_alerts";
    static constexpr size_t MaxStoredAlerts = 64;

    static AdminState &Instance();

    //Reads the flag, polls now and keeps polling.
    void start();
    void stop();

    Mode mode() const;

    //True when the admin rules apply: admin set, or presumed set (Closed).
    bool enforced() const;

    bool flagPresent() const { return seenSet; }
    void loadFlag();

    //Polls admin/status now; done runs once the state is updated. Calls
    //arriving while a poll is in flight share it.
    void refresh(std::function<void()> done = {});

    //Daemon answers that tell the state, wherever they come from.
    void observe(bool adminSet);
    void observeTooOld();
    void observeUnreachable();

    //Delay before the next poll: fast while the state is unknown in the first
    //two minutes, slow otherwise.
    static double pollDelay(double sinceStart, bool known);

    //At most one push per alert type in that window: a squatter of the
    //daemon's port must not reach every phone once per alert it makes up
    static constexpr double AlertPushInterval = 600.0;

    /* Stores the admin_set or admin_reset alert of the daemon as a persistent
     * EventPushNotification shown at the next login, once per id: the id is
     * saved in AlertsFileName, so a replay after a restart is not stored
     * again. False, stored never called, for an id stored before or another
     * type. stored runs once the history worker has written the event.
     */
    bool storeAlert(const string &type, const string &id, std::function<void()> stored = {});

    /* Pushes the alert to every registered token, at most once per type
     * every AlertPushInterval. done(true) when it went out, nobody had a
     * token or the cooldown skipped it; done(false) when it failed.
     */
    void pushAlert(const string &type, std::function<void(bool)> done = {});

    //Reads AlertsFileName again; the ids stored in memory are forgotten
    void loadAlerts();

    //Sets the push cooldown and forgets the pushes already sent
    void resetAlertPushes(double interval);

private:
    AdminState();

    enum class Reach
    {
        Unknown,
        Set,
        NotSet,
        TooOld
    };

    Reach reach = Reach::Unknown;
    bool seenSet = false;
    bool started = false;
    bool polling = false;
    bool failureLogged = false;
    double startTime = 0.0;
    Timer *pollTimer = nullptr;
    std::vector<std::function<void()>> waiting;
    std::deque<string> storedAlerts;
    map<string, double> lastAlertPush;
    double alertPushInterval = AlertPushInterval;

    string flagPath() const;
    string alertsPath() const;
    void saveAlerts();
    void setFlag(bool present);
    void scheduleNextPoll();
    void pollNow();
    void pollDone(const DaemonReply &reply);
};

} //namespace Calaos

#endif
