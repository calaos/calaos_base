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
#include "Utils.h"
#include "IOFactory.h"
#include "ReolinkCtrl.h"
#include "ReolinkWire.h"
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
        Params p;

        //Never log msg itself: this is the mirror of a channel that carries
        //credentials, and the byte count is what the python end logs too.
        if (!ReolinkWire::decodeMessage(msg, p))
        {
            cWarningDom("reolink") << "Error parsing json message ("
                                   << msg.size() << " bytes)";
            return;
        }

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

            cDebugDom("reolink") << "Event received from " << hostname << " type: " << event_type << " data: " << event_data;

            registry.dispatch(hostname, event_type, event_data);
        }
    });

    // Start the process immediately
    process->startProcess(exe, "reolink");
}

ReolinkCtrl::~ReolinkCtrl()
{
}

ReolinkCtrl::RegistrationId ReolinkCtrl::registerCamera(ReolinkTypes::Hostname hostname,
                                                        ReolinkTypes::Username username,
                                                        ReolinkTypes::Password password,
                                                        ReolinkTypes::EventType event_type,
                                                        EventReceivedSignal callback)
{
    /* T3.31 - the four fields are assembled ONCE, here, by name, and travel
     * as one value from this line on. The positional brace-init that used to
     * sit on the registry.add() below is gone: it was the one place in the
     * tree where the residual of an aggregate had actually been realised. */
    const ReolinkEventRegistry::CameraRegistration reg(std::move(hostname),
                                                       std::move(username),
                                                       std::move(password),
                                                       std::move(event_type));

    string camera_key = ReolinkEventRegistry::cameraKey(reg.hostname, reg.event_type);

    cDebugDom("reolink") << "Registering camera: " << reg.hostname << " for event: " << reg.event_type;

    // Add callback + store registration info for recovery after crashes
    RegistrationId id = registry.add(reg, std::move(callback));

    // Check if camera is already registered for this event type
    if (registeredCameras.find(camera_key) != registeredCameras.end())
    {
        cDebugDom("reolink") << "Camera " << reg.hostname << " already registered for event " << reg.event_type;
        return id;
    }

    // Send registration if connected, otherwise wait for connection
    if (connected)
    {
        doRegisterCamera(reg);
    }
    else
    {
        cDebugDom("reolink") << "Process not connected, camera will be registered when process connects";
    }

    return id;
}

void ReolinkCtrl::unregisterCamera(RegistrationId id)
{
    string camera_key;
    if (registry.remove(id, &camera_key))
    {
        // Last callback for this camera/event gone: forget the protocol-side
        // registration so a future registerCamera() re-sends it. The external
        // process keeps watching until it restarts, which is harmless.
        registeredCameras.erase(camera_key);
        cDebugDom("reolink") << "Last callback removed for camera " << camera_key;
    }
}

void ReolinkCtrl::doRegisterCamera(const ReolinkEventRegistry::CameraRegistration &reg)
{
    string camera_key = ReolinkEventRegistry::cameraKey(reg.hostname, reg.event_type);

    /* Register camera with the external process. The message carries the
     * camera password IN CLEAR: never log it, here or anywhere downstream.
     *
     * ⚠️ T3.31, residual n°1, DECLARED and not closed: these four wrappings
     * are the one place left where a human could name the wrong field -
     * ReolinkTypes::Username(reg.password) type-checks and always will. That
     * is intrinsic to wrapping: the typing collapses four unguarded hops into
     * this single line, it does not remove it. What it does remove is any
     * possibility of getting the ORDER wrong between here and the JSON. */
    process->sendMessage(ReolinkWire::buildRegisterMessage(
                             ReolinkTypes::Hostname(reg.hostname),
                             ReolinkTypes::Username(reg.username),
                             ReolinkTypes::Password(reg.password),
                             ReolinkTypes::EventType(reg.event_type)));

    // Mark camera as registered
    registeredCameras[camera_key] = camera_key;

    cInfoDom("reolink") << "Camera registration sent: " << reg.hostname << " for event " << reg.event_type;
}

void ReolinkCtrl::registerAllCameras()
{
    if (registry.empty())
    {
        cDebugDom("reolink") << "No cameras to register";
        return;
    }

    cInfoDom("reolink") << "Registering " << registry.cameraCount() << " cameras to process";

    registry.forEachRegistration([this](const ReolinkEventRegistry::CameraRegistration &reg)
    {
        cDebugDom("reolink") << "Registering camera: " << reg.hostname << " for event: " << reg.event_type;
        doRegisterCamera(reg);
    });
}

