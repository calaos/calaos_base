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
#include <cstdint>
#include <map>
#include <memory>

using namespace Utils;

namespace Calaos
{

class RemoteUIWebSocketHandler;
class RemoteUI;

/* When the server may offer a firmware. An offer starts a download the server
 * has to serve to the end, so none is made while the server is likely to go
 * away: right after it started (an upgrade restarts it, possibly more than
 * once) and while a package transaction is running, which the rescan request
 * of the firmware package postinst reveals. Each event only pushes the date
 * later.
 * Header-inline so it can be unit-tested without linking server objects.
 */
class OtaOfferGate
{
public:
    void holdFor(uint64_t nowMs, uint64_t delayMs)
    {
        if (nowMs + delayMs > holdUntilMs)
            holdUntilMs = nowMs + delayMs;
    }

    bool offersAllowed(uint64_t nowMs) const { return nowMs >= holdUntilMs; }

    uint64_t msUntilAllowed(uint64_t nowMs) const
    {
        return offersAllowed(nowMs) ? 0 : holdUntilMs - nowMs;
    }

private:
    uint64_t holdUntilMs = 0;
};

class OtaFirmwareManager
{
private:
    static OtaFirmwareManager *_instance;

    // Cache of firmware manifests indexed by hardware_id
    std::map<string, FirmwareManifest> firmwareCache;

    // Timer for periodic rescan
    std::shared_ptr<uvw::TimerHandle> rescanTimer;

    OtaOfferGate offerGate;
    // Offers the devices refused while the gate was closed, once it opens
    std::shared_ptr<uvw::TimerHandle> deferredNotifyTimer;
    uint64_t startupHoldMs = DEFAULT_STARTUP_HOLD_MS;
    uint64_t packageHoldMs = DEFAULT_PACKAGE_HOLD_MS;

    // Configuration
    string firmwarePath;
    bool enabled;
    int rescanIntervalMinutes;

    OtaFirmwareManager();

    // Internal methods
    void setupPeriodicRescan();
    void scanDirectory(const string &path);
    void scheduleDeferredNotify();

public:
    static constexpr uint64_t DEFAULT_STARTUP_HOLD_MS = 60 * 1000;
    static constexpr uint64_t DEFAULT_PACKAGE_HOLD_MS = 120 * 1000;

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

    // Holds offers for the startup delay; init() calls it
    void serverStarted();

    // Rescan asked by a firmware package being installed: the new image is
    // served at once, but offered only once the transaction has settled.
    void rescanFromPackage();

    // The update message for a device, null when it is up to date or when
    // no firmware matches. Ignores the offer gate.
    Json buildOffer(const string &hardwareId, const string &currentVersion) const;

    bool offersAllowed() const { return offerGate.offersAllowed(nowMs()); }
    bool deferredNotifyPending() const;

    // Apply to the holds started after the call
    void setHoldDelays(uint64_t startupMs, uint64_t packageMs)
    {
        startupHoldMs = startupMs;
        packageHoldMs = packageMs;
    }

    static uint64_t nowMs();

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

    /* Rescan period in milliseconds, computed in 64-bit and clamped.
     * minutes * 60 * 1000 in int overflows (UB) above ~35791 minutes and
     * would yield a wrong (possibly tiny) timer period.
     * Clamped to [1 minute, 30 days].
     * Header-inline so it can be unit-tested without linking server objects.
     */
    static uint64_t computeRescanIntervalMs(int minutes)
    {
        static const uint64_t maxMs = 30ull * 24 * 60 * 60 * 1000; //30 days
        if (minutes < 1)
            minutes = 1;
        uint64_t ms = static_cast<uint64_t>(minutes) * 60ull * 1000ull;
        if (ms > maxMs)
            ms = maxMs;
        return ms;
    }

    // Get the firmware path
    const string &getFirmwarePath() const { return firmwarePath; }
};

}

#endif
