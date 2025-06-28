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
#include "Timer.h"

class ReolinkCtrl : public sigc::trackable
{
public:
    typedef sigc::slot<void, string, string, string> EventReceivedSignal;

private:
    ReolinkCtrl();
    ~ReolinkCtrl();

    ExternProcServer *process;
    string exe;

    unordered_map<string, vector<EventReceivedSignal>> eventCallbacks;
    unordered_map<string, string> registeredCameras;

    bool connected = false;

    // Store all camera registrations for recovery after crash
    struct CameraRegistration {
        string hostname;
        string username;
        string password;
        string event_type;
    };
    unordered_map<string, CameraRegistration> allRegistrations;

    string generateCameraKey(const string &hostname, const string &event_type);
    void doRegisterCamera(const string &hostname, const string &username, const string &password, const string &event_type);
    void registerAllCameras();

public:
    void registerCamera(const string hostname, const string username, const string password, const string event_type, EventReceivedSignal callback);

    bool isConnected() const { return connected; }

    static ReolinkCtrl &Instance()
    {
        static ReolinkCtrl instance;
        return instance;
    }
};

#endif // __REOLINK_CTRL_H__