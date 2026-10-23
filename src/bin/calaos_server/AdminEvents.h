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
    };

    //A daemon frame bigger than this is a protocol error
    static constexpr size_t MaxMessageSize = 1024 * 1024;
    static constexpr size_t MaxHandshakeSize = 16 * 1024;

    static AdminEvents &Instance();

    void start();
    void stop();

    bool connected() const { return state == State::Open; }

    void setTimings(const Timings &t) { timings = t; }

    //The data member of an admin_event for every logged-in household client
    sigc::signal<void, const Json &> householdEvent;

    /* The admin_event data for a daemon event, null when it is not forwarded.
     * Only upgrade_progress, admin_set and admin_reset are: the job, the push
     * codes and any future type stay on this side.
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
        Waiting,     //reconnect timer armed
        Connecting,  //TCP connect and handshake
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
    void ack(const string &id);

    void sendFrame(int opcode, const string &payload);
    void write(const string &bytes);
};

} //namespace Calaos

#endif
