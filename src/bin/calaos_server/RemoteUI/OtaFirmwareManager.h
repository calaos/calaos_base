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
#ifndef OTAFIRMWAREMANAGER_H
#define OTAFIRMWAREMANAGER_H

#include "Utils.h"
#include "libuvw.h"
#include "FirmwareManifest.h"
#include <map>
#include <memory>

using namespace Utils;

namespace Calaos
{

class RemoteUIWebSocketHandler;
class RemoteUI;

class OtaFirmwareManager
{
private:
    static OtaFirmwareManager *_instance;

    // Cache of firmware manifests indexed by hardware_id
    std::map<string, FirmwareManifest> firmwareCache;

    // Timer for periodic rescan
    std::shared_ptr<uvw::TimerHandle> rescanTimer;

    // Configuration
    string firmwarePath;
    bool enabled;
    int rescanIntervalMinutes;

    OtaFirmwareManager();

    // Internal methods
    void setupPeriodicRescan();
    void scanDirectory(const string &path);

public:
    static OtaFirmwareManager &Instance()
    {
        if (!_instance)
            _instance = new OtaFirmwareManager();
        return *_instance;
    }

    ~OtaFirmwareManager();

    // Initialize and perform initial scan
    void init();

    // Rescan firmware directory and notify affected devices
    void rescan();

    // Get firmware manifest by hardware_id
    const FirmwareManifest *getFirmware(const string &hardwareId) const;

    // Check if a device needs an update and send notification via WebSocket
    // Returns true if notification was sent
    bool checkDeviceForUpdate(RemoteUIWebSocketHandler *handler,
                             const string &hardwareId,
                             const string &currentVersion);

    // Notify all connected devices about available updates
    void notifyAllDevices();

    // Get all available firmware manifests
    const std::map<string, FirmwareManifest> &getAllFirmwares() const { return firmwareCache; }

    // Check if OTA is enabled
    bool isEnabled() const { return enabled; }

    // Get the firmware path
    const string &getFirmwarePath() const { return firmwarePath; }
};

}

#endif
