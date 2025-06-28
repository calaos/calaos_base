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
#include <json.hpp>

#include "Utils.h"
#include "IOFactory.h"
#include "ReolinkCtrl.h"
#include "Prefix.h"
#include "Params.h"

using namespace Calaos;

ReolinkCtrl::ReolinkCtrl()
{
    cDebugDom("reolink") << "New Reolink external process";
    process = new ExternProcServer("reolink");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_reolink";

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("reolink") << "Reolink process exited, restarting...";
        connected = false;
        registeredCameras.clear();
        process->startProcess(exe, "reolink", "");
    });

    process->messageReceived.connect([=](const string &msg)
    {
        json_error_t jerr;
        json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

        if (!jroot)
        {
            cWarningDom("reolink") << "Error parsing json: " << jerr.text;
            if (jroot)
                json_decref(jroot);
            return;
        }

        Params p;
        jansson_decode_object(jroot, p);

        if (p.Exists("status"))
        {
            if (p["status"] == "connected")
            {
                connected = true;
                cInfoDom("reolink") << "Reolink process connected";

                // Re-register all cameras when process connects/reconnects
                registerAllCameras();
            }
            else if (p["status"] == "error")
            {
                cErrorDom("reolink") << "Reolink process error: " << p["message"];
            }
        }
        else if (p.Exists("event") && p.Exists("hostname") && p.Exists("event_type"))
        {
            string hostname = p["hostname"];
            string event_type = p["event_type"];
            string event_data = p["event"];
            string camera_key = generateCameraKey(hostname, event_type);

            cDebugDom("reolink") << "Event received from " << hostname << " type: " << event_type << " data: " << event_data;

            auto it = eventCallbacks.find(camera_key);
            if (it != eventCallbacks.end())
            {
                for (auto &callback : it->second)
                {
                    callback(hostname, event_type, event_data);
                }
            }
        }

        json_decref(jroot);
    });

    // Start the process immediately
    process->startProcess(exe, "reolink");
}

ReolinkCtrl::~ReolinkCtrl()
{
}

void ReolinkCtrl::registerCamera(const string hostname, const string username, const string password, const string event_type, EventReceivedSignal callback)
{
    string camera_key = generateCameraKey(hostname, event_type);

    cDebugDom("reolink") << "Registering camera: " << hostname << " for event: " << event_type;

    // Add callback to the list
    auto v = eventCallbacks[camera_key];
    v.push_back(callback);
    eventCallbacks[camera_key] = v;

    // Store registration info for recovery after crashes
    allRegistrations[camera_key] = {hostname, username, password, event_type};

    // Check if camera is already registered for this event type
    if (registeredCameras.find(camera_key) != registeredCameras.end())
    {
        cDebugDom("reolink") << "Camera " << hostname << " already registered for event " << event_type;
        return;
    }

    // Send registration if connected, otherwise wait for connection
    if (connected)
    {
        doRegisterCamera(hostname, username, password, event_type);
    }
    else
    {
        cDebugDom("reolink") << "Process not connected, camera will be registered when process connects";
    }
}

void ReolinkCtrl::doRegisterCamera(const string &hostname, const string &username, const string &password, const string &event_type)
{
    string camera_key = generateCameraKey(hostname, event_type);

    // Register camera with the external process
    json_t *jroot = json_object();
    json_object_set_new(jroot, "action", json_string("register"));
    json_object_set_new(jroot, "hostname", json_string(hostname.c_str()));
    json_object_set_new(jroot, "username", json_string(username.c_str()));
    json_object_set_new(jroot, "password", json_string(password.c_str()));
    json_object_set_new(jroot, "event_type", json_string(event_type.c_str()));

    string message = jansson_to_string(jroot);
    process->sendMessage(message);
    json_decref(jroot);

    // Mark camera as registered
    registeredCameras[camera_key] = camera_key;

    cInfoDom("reolink") << "Camera registration sent: " << hostname << " for event " << event_type;
}

void ReolinkCtrl::registerAllCameras()
{
    if (allRegistrations.empty())
    {
        cDebugDom("reolink") << "No cameras to register";
        return;
    }

    cInfoDom("reolink") << "Registering " << allRegistrations.size() << " cameras to process";

    for (auto &pair : allRegistrations)
    {
        const CameraRegistration &reg = pair.second;
        cDebugDom("reolink") << "Registering camera: " << reg.hostname << " for event: " << reg.event_type;
        doRegisterCamera(reg.hostname, reg.username, reg.password, reg.event_type);
    }
}

string ReolinkCtrl::generateCameraKey(const string &hostname, const string &event_type)
{
    return hostname + "_" + event_type;
}

