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
#include "AVRRose.h"
#include "AVRRoseNotifServer.h"
#include "UrlDownloader.h"
#include <tcpsocket.h>
#include <chrono>

using namespace Calaos;

const int HIFIROSE_DEFAULT_PORT = 9283;
const double POLL_INTERVAL = 30.0; // Fallback polling interval in seconds

/*
HifiRose devices (RS520, etc.) use an HTTPS REST API on port 9283
with a self-signed certificate (HiFiRose Root CA).
Push notifications are sent by the device to our HTTP server on port 9284.
*/

AVRRoseNotifServer *AVRRose::notifServer = nullptr;
int AVRRose::notifServerRefCount = 0;

AVRRose::AVRRose(Params &p):
    AVReceiver(p, HIFIROSE_DEFAULT_PORT, AVR_CON_CUSTOM)
{
    cInfoDom("hifirose") << "Initializing HiFi Rose device at " << host;

    // Initialize last notification time to now so we don't immediately re-register
    lastNotifTime = std::chrono::steady_clock::now();

    // Start the shared notification server if not already running
    notifServerRefCount++;
    if (!notifServer)
        notifServer = new AVRRoseNotifServer();

    notifServer->registerReceiver(this);

    // Determine our local IP for push notification registration
    localIP = TCPSocket::GetLocalIPFor(host);
    cInfoDom("hifirose") << "Local IP for push notifications: " << localIP;

    // Register with the device so it sends us push notifications
    registerDevice();

    // Start fallback polling timer
    pollTimer = new Timer(POLL_INTERVAL, [this]()
    {
        pollStatus();
    });

    // Initial status poll
    pollStatus();
}

AVRRose::~AVRRose()
{
    delete pollTimer;
    pollTimer = nullptr;

    if (notifServer)
        notifServer->unregisterReceiver(this);

    notifServerRefCount--;
    if (notifServerRefCount <= 0)
    {
        delete notifServer;
        notifServer = nullptr;
        notifServerRefCount = 0;
    }
}

void AVRRose::registerDevice()
{
    if (localIP.empty())
    {
        cWarningDom("hifirose") << "No local IP available, cannot register for push notifications";
        return;
    }

    Json d = {
        { "connectIP", localIP }
    };

    postRequest("device_connected", d.dump(), [this](const string &data)
    {
        try
        {
            Json jdoc = Json::parse(data);
            if (jdoc.contains("data") && jdoc["data"].contains("deviceRoseToken"))
                roseToken = jdoc["data"]["deviceRoseToken"].get<string>();

            cInfoDom("hifirose") << "Registered with device, roseToken: " << roseToken;

            // Reset notification timer after successful registration
            lastNotifTime = std::chrono::steady_clock::now();
        }
        catch (const std::exception &e)
        {
            cWarningDom("hifirose") << "Failed to parse device_connected response: " << e.what();
            // Retry registration after a delay
            Timer::singleShot(10.0, [this]()
            {
                registerDevice();
            });
        }
    });
}

void AVRRose::reregisterIfNeeded()
{
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - lastNotifTime).count();

    if (elapsed > NOTIF_TIMEOUT)
    {
        cInfoDom("hifirose") << "No push notification received for " << elapsed
                              << "s, re-registering with device";
        registerDevice();
    }
}

void AVRRose::Power(bool on, int zone)
{
    pollStatus([on, this]()
    {
        // Only toggle if the current state differs from the desired state
        if ((on && power_main) ||
            (!on && !power_main))
            return;

        Json d = {
            { "barControl", "remote_bar_order_sleep_on_off" },
            { "value", -1 }
        };

        postRequest("remote_bar_order", d.dump(), [this](const string &)
        {
            pollStatus();
        });
    });
}

void AVRRose::setVolume(int volume, int zone)
{
    Json d = {
        { "volume", volume }
    };

    postRequest("volume", d.dump(), [this](const string &)
    {
        // POST /volume returns an empty body, refresh state via get_control_info
        pollStatus();
    });
}

void AVRRose::selectInputSource(int source, int zone)
{
}

void AVRRose::sendCustomCommand(string command)
{
    if (command == "play" || command == "pause")
    {
        Json d = { { "currentPlayState", 17 } }; // Play/Pause toggle
        postRequest("current_play_state", d.dump(), [this](const string &) { pollStatus(); });
    }
    else if (command == "next")
    {
        Json d = { { "currentPlayState", 18 } }; // Next track
        postRequest("current_play_state", d.dump(), [this](const string &) { pollStatus(); });
    }
    else if (command == "prev")
    {
        Json d = { { "currentPlayState", 19 } }; // Previous track
        postRequest("current_play_state", d.dump(), [this](const string &) { pollStatus(); });
    }
    else if (command == "repeat")
    {
        Json d = { { "currentPlayState", 24 } }; // Toggle repeat
        postRequest("current_play_state", d.dump(), nullptr);
    }
    else if (command == "shuffle")
    {
        Json d = { { "currentPlayState", 25 } }; // Toggle shuffle
        postRequest("current_play_state", d.dump(), nullptr);
    }
    else if (command == "mute")
    {
        Json d = {
            { "barControl", "remote_bar_order.mute" },
            { "value", -1 }
        };
        postRequest("remote_bar_order", d.dump(), [this](const string &) { pollStatus(); });
    }
    else
    {
        cWarningDom("hifirose") << "Unknown custom command: " << command;
    }
}

void AVRRose::handleNotification(const Json &msg)
{
    if (!msg.contains("messageType"))
    {
        cWarningDom("hifirose") << "Notification without messageType";
        return;
    }

    string msgType = msg["messageType"].get<string>();

    // Update last notification timestamp — the device is alive and knows about us
    lastNotifTime = std::chrono::steady_clock::now();

    cDebugDom("hifirose") << "Push notification: " << msgType;

    if (msgType == "volume_change")
    {
        if (msg.contains("volume"))
        {
            volume_main = msg["volume"].get<int>();
            state_changed_1.emit("volume", Utils::to_string(volume_main));
            cDebugDom("hifirose") << "Volume changed to " << volume_main;
        }
    }
    else if (msgType == "mute_state_change_noti")
    {
        if (msg.contains("position"))
        {
            mute_main = (msg["position"].get<int>() != 0);
            state_changed_1.emit("mute", mute_main ? "true" : "false");
            cDebugDom("hifirose") << "Mute changed to " << (mute_main ? "true" : "false");
        }
    }
    else if (msgType == "music_start")
    {
        cDebugDom("hifirose") << "Playback started, refreshing state";
        pollStatus();
    }
    else if (msgType == "state_check")
    {
        cDebugDom("hifirose") << "Heartbeat from device";
    }
    else if (msgType == "out_put_change")
    {
        cDebugDom("hifirose") << "Output changed";
    }
    else
    {
        cDebugDom("hifirose") << "Unhandled notification type: " << msgType;
    }
}

void AVRRose::pollStatus(std::function<void()> nextCb)
{
    postRequest("get_current_state", {}, [this, nextCb](const string &data)
    {
        Json jdoc;
        try
        {
            jdoc = Json::parse(data);
            if (!jdoc.is_object())
            {
                cWarningDom("hifirose") << "Invalid JSON data from get_current_state";
                return;
            }

            string code = jdoc["code"].get<string>();

            if (code == "SLEEP")
            {
                power_main = false;
                state_changed_1.emit("power", "false");
                deviceReachable = true;

                // Device is sleeping, no point querying volume/mute
                if (nextCb)
                    nextCb();
                return;
            }
            else if (code == "G0000")
            {
                bool wasUnreachable = !deviceReachable;
                power_main = true;
                deviceReachable = true;
                state_changed_1.emit("power", "true");

                // Device just came back — likely rebooted, re-register immediately
                if (wasUnreachable)
                {
                    cInfoDom("hifirose") << "Device became reachable again, re-registering";
                    registerDevice();
                }
                else
                {
                    // Device was already reachable, check if notifications stopped
                    reregisterIfNeeded();
                }
            }
            else
            {
                cWarningDom("hifirose") << "Unknown status code: " << code;
            }
        }
        catch (const std::exception &e)
        {
            cWarningDom("hifirose") << "Error parsing get_current_state: " << e.what();
            deviceReachable = false;
            if (nextCb)
                nextCb();
            return;
        }

        // Device is awake — get volume via GET /get_control_info
        getRequest("get_control_info", [this, nextCb](const string &dataRes)
        {
            try
            {
                Json jctrl = Json::parse(dataRes);
                if (jctrl.is_object() && jctrl.contains("volumeValue"))
                {
                    volume_main = jctrl["volumeValue"].get<int>();
                    state_changed_1.emit("volume", Utils::to_string(volume_main));
                }
            }
            catch (const std::exception &e)
            {
                cWarningDom("hifirose") << "Error parsing get_control_info: " << e.what();
            }

            // Get mute state
            postRequest("mute.state.get", {}, [this, nextCb](const string &muteRes)
            {
                try
                {
                    Json jmute = Json::parse(muteRes);
                    if (jmute.is_object() && jmute.contains("mute"))
                    {
                        mute_main = (jmute["mute"].get<int>() != 0);
                        state_changed_1.emit("mute", mute_main ? "true" : "false");
                    }
                }
                catch (const std::exception &e)
                {
                    cWarningDom("hifirose") << "Error parsing mute.state.get: " << e.what();
                }

                if (nextCb)
                    nextCb();
            });
        });
    });
}

void AVRRose::postRequest(string urlPath, string data, std::function<void(const string &)> dataCb)
{
    string url = "https://" + host + ":" + Utils::to_string(port) + "/" + urlPath;

    cDebugDom("hifirose") << "POST " << url << " data: " << data;

    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->setHeader("Content-Type", "application/json;charset=utf-8");
    dl->httpPost({}, data);

    dl->m_signalCompleteData.connect([dataCb, urlPath](const string &dataRes, int status)
    {
        if (status != 200)
        {
            cErrorDom("hifirose") << "POST " << urlPath << " failed with status: " << status;
            return;
        }

        if (dataCb)
            dataCb(dataRes);
    });
}

void AVRRose::getRequest(string urlPath, std::function<void(const string &)> dataCb)
{
    string url = "https://" + host + ":" + Utils::to_string(port) + "/" + urlPath;

    cDebugDom("hifirose") << "GET " << url;

    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->httpGet();

    dl->m_signalCompleteData.connect([dataCb, urlPath](const string &dataRes, int status)
    {
        if (status != 200)
        {
            cErrorDom("hifirose") << "GET " << urlPath << " failed with status: " << status;
            return;
        }

        if (dataCb)
            dataCb(dataRes);
    });
}
