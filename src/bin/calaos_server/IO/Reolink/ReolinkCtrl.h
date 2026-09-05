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
#ifndef __REOLINK_CTRL_H__
#define __REOLINK_CTRL_H__

#include <unordered_map>

#include "Params.h"
#include "Utils.h"
#include "IODoc.h"

#include "Calaos.h"
#include "ExternProc.h"
#include "IOBase.h"
#include "ReolinkEventRegistry.h"
#include "Timer.h"

class ReolinkCtrl : public sigc::trackable
{
public:
    using RegistrationId = ReolinkEventRegistry::RegistrationId;
    using EventReceivedSignal = ReolinkEventRegistry::EventCallback;

private:
    ReolinkCtrl();
    ~ReolinkCtrl();

    ExternProcServer *process;
    string exe;

    // Callback bookkeeping + camera registrations (recovery after crash)
    ReolinkEventRegistry registry;

    // Cameras whose registration has been sent to the external process
    unordered_map<string, string> registeredCameras;

    bool connected = false;

    //T3.31 - one registration, not four loose strings: there is no order
    //left to get wrong between here and the wire.
    //Whether a hostname is one this controller registered. What a sidecar
    //echoes back is not, on its own, a value this end can vouch for.
    bool isRegisteredHostname(const string &hostname) const;

    void doRegisterCamera(const ReolinkEventRegistry::CameraRegistration &reg);
    void registerAllCameras();

public:
    // Returns an id that MUST be passed to unregisterCamera() before the
    // callback's owner is destroyed, otherwise the stored callback dangles.
    //T3.31 - four DISTINCT types, never four bare strings. A caller that
    //hands hostname/username/password/event_type in the wrong order no
    //longer compiles; before, it produced a perfectly well formed message
    //that sent the password in clear in the username field.
    RegistrationId registerCamera(ReolinkTypes::Hostname hostname,
                                  ReolinkTypes::Username username,
                                  ReolinkTypes::Password password,
                                  ReolinkTypes::EventType event_type,
                                  EventReceivedSignal callback);
    void unregisterCamera(RegistrationId id);

    bool isConnected() const { return connected; }

    static ReolinkCtrl &Instance()
    {
        static ReolinkCtrl instance;
        return instance;
    }
};

#endif // __REOLINK_CTRL_H__