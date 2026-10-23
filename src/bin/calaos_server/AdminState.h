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

    /* Records the admin_set or admin_reset alert of the daemon: a push to
     * every registered token and a persistent EventPushNotification shown at
     * the next login. False when this id was already recorded, since the
     * daemon replays an alert until it is acknowledged.
     */
    bool recordAlert(const string &type, const string &id);

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
    std::deque<string> recordedAlerts;

    string flagPath() const;
    void setFlag(bool present);
    void scheduleNextPoll();
    void pollNow();
    void pollDone(const DaemonReply &reply);
};

} //namespace Calaos

#endif
