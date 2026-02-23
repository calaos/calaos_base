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
#ifndef AVRRose_H
#define AVRRose_H

#include "Calaos.h"
#include "AVReceiver.h"
#include "Timer.h"
#include <chrono>

namespace Calaos
{

class AVRRoseNotifServer;

//Manage HifiRose devices (like RS520)
//Uses HTTPS REST API on port 9283 and push notifications on port 9284
class AVRRose: public AVReceiver
{
protected:

    bool mute_main = false;
    bool deviceReachable = false;
    string localIP;
    string roseToken;
    Timer *pollTimer = nullptr;

    // Track the last time we received a push notification from the device.
    // If too much time passes without one, we assume the device rebooted
    // and re-register for push notifications.
    std::chrono::steady_clock::time_point lastNotifTime;
    static constexpr double NOTIF_TIMEOUT = 90.0; // seconds without notification before re-registering

    void postRequest(string urlPath, string data, std::function<void(const string &)> dataCb);
    void getRequest(string urlPath, std::function<void(const string &)> dataCb);
    void pollStatus(std::function<void()> nextCb = nullptr);
    void registerDevice();
    void reregisterIfNeeded();

    static AVRRoseNotifServer *notifServer;
    static int notifServerRefCount;

public:
    AVRRose(Params &p);
    virtual ~AVRRose();

    virtual void Power(bool on, int zone = 1) override;
    virtual void setVolume(int volume, int zone = 1) override;
    virtual void selectInputSource(int source, int zone = 1) override;
    virtual bool hasDisplay() override { return false; }

    virtual void sendCustomCommand(string command) override;

    //Called by the notification server when a push notification arrives
    void handleNotification(const Json &msg);

    string getHost() const { return host; }
};

}

#endif // AVRRose_H
