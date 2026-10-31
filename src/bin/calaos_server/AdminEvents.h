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
#ifndef S_AdminEvents_H
#define S_AdminEvents_H

#include "Calaos.h"

#include <deque>
#include <memory>
#include <set>
#include <sigc++/sigc++.h>

class Timer;
class WebSocketFrame;

namespace uvw
{
class TcpHandle;
}

namespace Calaos
{

/* The persistent subscription of calaos_server to the daemon's
 * /api/v2/events (docs/API.md of calaos-container, Events). Household clients
 * get a filtered view of it as admin_event (docs/08_http_api.md); the
 * admin_set and admin_reset alerts are recorded once and acknowledged.
 */
class AdminEvents
{
public:
    struct Timings
    {
        double ping = 15.0;    //our ping, the daemon pings every 30 s
        double silence = 75.0; //nothing received for that long: reconnect
        double minBackoff = 1.0;
        double maxBackoff = 60.0;
        double retryDelay = 60.0; //first retry of a lost push or ack, then doubled
        int maxRetries = 5;
        //Push codes fetched per rolling hour: twice the daemon's own budget,
        //so a squatter of its port cannot flood the phones with its texts
        size_t maxCodePushes = 20;
        //How long the key that sealed an approver_confirm code waits for its approver_set
        double provedKeyLife = 600.0;
    };

    //A daemon frame bigger than this is a protocol error
    static constexpr size_t MaxMessageSize = 1024 * 1024;
    static constexpr size_t MaxHandshakeSize = 16 * 1024;
    //An alert id we cannot use is still acked when it is at most this long
    static constexpr size_t MaxForeignAlertId = 256;
    static constexpr size_t MaxRemembered = 64;
    static constexpr size_t MaxCodeMessage = 256;

    static AdminEvents &Instance();

    void start();
    void stop();

    bool connected() const { return state == State::Open; }

    //Also forgets the push codes already counted
    void setTimings(const Timings &t)
    {
        timings = t;
        codePushes.clear();
        provedKeys.clear();
    }

    //For the tests: the retry series held, and their timers fired after delay
    size_t retrySeries() const { return retries.size(); }
    void rearmRetries(double delay);

    //The data member of an admin_event for every logged-in household client
    sigc::signal<void, const Json &> householdEvent;

    /* The admin_event data for a daemon event, null when it is not forwarded.
     * Only upgrade_progress, the alerts (admin_set, admin_reset, approver_set,
     * approver_removed, console_opened, ts only; network_rolled_back with its
     * intf) and network_pending / network_applied are: the job, the push codes
     * and any future type stay on this side.
     */
    static Json householdEventFor(const Json &event, const string &receivedAt);

    /* <Status> and <Job> without the package inventory: a household client
     * learns how far an upgrade is, not what the box runs. Fields are copied
     * from an allow list, so one the daemon adds later is not leaked.
     */
    static Json householdStatus(const Json &status);
    static Json householdJob(const Json &job);

    //Doubles, from min up to max
    static double nextBackoff(double current, const Timings &t);

private:
    AdminEvents();

    enum class State
    {
        Stopped,
        Waiting,    //reconnect timer armed
        Connecting, //TCP connect and handshake
        Open,
    };

    State state = State::Stopped;
    Timings timings;
    double backoff = 0.0;
    bool tokenRetried = false;
    bool failureLogged = false;
    bool tooOldLogged = false;

    std::shared_ptr<uvw::TcpHandle> handle;
    //Bumped on every connection: callbacks of an older one are ignored
    unsigned long generation = 0;
    double connectedAt = 0.0;
    double lastActivity = 0.0;
    string key;
    string inbuf;
    string message;
    bool inMessage = false;
    bool binaryMessage = false;
    std::unique_ptr<WebSocketFrame> frame;

    Timer *reconnectTimer = nullptr;
    Timer *watchdogTimer = nullptr;

    std::set<string> acking;
    std::set<string> storing;
    std::set<string> unusableAlerts;

    void connect();
    void onConnected(uvw::TcpHandle &h, const string &host, int port);
    void scheduleReconnect(double delay);
    void reconnectNow();
    //delay < 0: the next backoff step
    void drop(const string &why, double delay = -1.0);
    void watchdog();

    void onData(const char *data, size_t len);
    bool processHandshake();
    void processFrames();
    void processMessage(const string &text);
    void processAlert(const string &type, const Json &data);
    //Keeps the name and format of an approver's push_tokens row as enrolled,
    //and the key it proved by reading its approver_confirm code
    void markApprover(const string &tokenId, bool approver);
    struct ProvedKey
    {
        string key;
        double at = 0.0;
    };
    //By token_id
    map<string, ProvedKey> provedKeys;
    void rememberProvedKey(const string &tokenId, const string &key);
    //Fetches the code's message by its id, pushes it to the approver's token only
    void processPushCode(const Json &data);
    void push(const string &type, const string &id, const Json &data);
    std::deque<double> codePushes;
    void ack(const string &id);

    enum class RetryKind
    {
        Push,
        Ack,
    };
    struct Retry
    {
        Timer *timer = nullptr;
        int attempts = 0;
    };
    //Keyed by kind and id: one alert may wait for its push and its ack
    map<string, Retry> retries;
    void scheduleRetry(RetryKind kind, const string &type, const string &id, const Json &data);
    void retryNow(RetryKind kind, string type, string id, Json data);
    void forgetRetry(RetryKind kind, const string &id);

    void sendFrame(int opcode, const string &payload);
    void write(const string &bytes);
};

} //namespace Calaos

#endif
