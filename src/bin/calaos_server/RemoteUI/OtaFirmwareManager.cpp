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
#include "OtaFirmwareManager.h"
#include "RemoteUIWebSocketHandler.h"
#include "RemoteUIManager.h"
#include "IO/RemoteUI/RemoteUI.h"
#include "Prefix.h"
#include "FileUtils.h"
#include <dirent.h>
#include <chrono>

static const char *TAG = "ota";

using namespace Calaos;

OtaFirmwareManager *OtaFirmwareManager::_instance = nullptr;

// Default values
static const string DEFAULT_FIRMWARE_PATH = "/usr/share/calaos/firmwares";
static const int DEFAULT_RESCAN_INTERVAL_MINUTES = 60;

OtaFirmwareManager::OtaFirmwareManager():
    enabled(true),
    rescanIntervalMinutes(DEFAULT_RESCAN_INTERVAL_MINUTES)
{
}

OtaFirmwareManager::~OtaFirmwareManager()
{
    if (rescanTimer)
        rescanTimer->stop();
}

uint64_t OtaFirmwareManager::nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void OtaFirmwareManager::init()
{
    // Check if OTA is enabled
    string enabledStr = Utils::get_config_option("ota_enabled");
    if (!enabledStr.empty())
        enabled = (enabledStr == "true" || enabledStr == "1");

    if (!enabled)
    {
        cInfoDom(TAG) << "OTA firmware updates disabled by configuration";
        return;
    }

    // Get firmware path from config or use default
    firmwarePath = Utils::get_config_option("ota_firmware_path");
    if (firmwarePath.empty())
    {
#ifdef OTA_FIRMWARE_PATH
        firmwarePath = OTA_FIRMWARE_PATH;
#else
        firmwarePath = DEFAULT_FIRMWARE_PATH;
#endif
    }

    // Get rescan interval from config
    string intervalStr = Utils::get_config_option("ota_rescan_interval");
    if (!intervalStr.empty())
    {
        try
        {
            rescanIntervalMinutes = std::stoi(intervalStr);
            if (rescanIntervalMinutes < 1)
                rescanIntervalMinutes = DEFAULT_RESCAN_INTERVAL_MINUTES;
        }
        catch (...)
        {
            rescanIntervalMinutes = DEFAULT_RESCAN_INTERVAL_MINUTES;
        }
    }

    cInfoDom(TAG) << "OTA firmware manager initializing with path: " << firmwarePath;
    cInfoDom(TAG) << "Rescan interval: " << rescanIntervalMinutes << " minutes";

    serverStarted();

    // Perform initial scan
    rescan();

    // Setup periodic rescan
    setupPeriodicRescan();
}

void OtaFirmwareManager::setupPeriodicRescan()
{
    auto loop = uvw::Loop::getDefault();

    rescanTimer = loop->resource<uvw::TimerHandle>();
    rescanTimer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &, uvw::TimerHandle &)
    {
        cDebugDom(TAG) << "Periodic firmware rescan triggered";
        rescan();
    });

    // Convert minutes to milliseconds (64-bit, clamped: the naive signed
    // multiply overflows above ~35791 minutes)
    uint64_t intervalMs = computeRescanIntervalMs(rescanIntervalMinutes);
    rescanTimer->start(uvw::TimerHandle::Time{intervalMs}, uvw::TimerHandle::Time{intervalMs});
}

void OtaFirmwareManager::rescan()
{
    if (!enabled)
        return;

    cInfoDom(TAG) << "Scanning firmware directory: " << firmwarePath;

    // Clear existing cache
    firmwareCache.clear();

    // Scan directory
    scanDirectory(firmwarePath);

    cInfoDom(TAG) << "Firmware scan complete. Found " << firmwareCache.size() << " firmware(s)";

    // Log all found firmwares
    for (const auto &pair : firmwareCache)
    {
        cDebugDom(TAG) << "Found firmware: " << pair.first << " v" << pair.second.getVersion();
    }

    // Notify all connected devices about available updates
    notifyAllDevices();
}

void OtaFirmwareManager::serverStarted()
{
    // An upgrade can restart the server more than once: wait before
    // starting downloads it may not be there to finish.
    offerGate.holdFor(nowMs(), startupHoldMs);
}

void OtaFirmwareManager::rescanFromPackage()
{
    // The package postinst runs inside the apt transaction, which may go on
    // with calaos-server itself and restart it.
    offerGate.holdFor(nowMs(), packageHoldMs);
    cInfoDom(TAG) << "Firmware package installed, offers held for "
                  << packageHoldMs / 1000 << " s";
    rescan();
}

bool OtaFirmwareManager::deferredNotifyPending() const
{
    return deferredNotifyTimer && deferredNotifyTimer->active();
}

void OtaFirmwareManager::scheduleDeferredNotify()
{
    const uint64_t delay = offerGate.msUntilAllowed(nowMs());
    if (!deferredNotifyTimer)
    {
        deferredNotifyTimer = uvw::Loop::getDefault()->resource<uvw::TimerHandle>();
        deferredNotifyTimer->on<uvw::TimerEvent>([this](const uvw::TimerEvent &, uvw::TimerHandle &)
        {
            deferredNotifyTimer->stop();
            notifyAllDevices();
        });
    }
    else if (deferredNotifyTimer->active())
    {
        deferredNotifyTimer->stop();
    }
    deferredNotifyTimer->start(uvw::TimerHandle::Time{delay + 1}, uvw::TimerHandle::Time{0});
}

void OtaFirmwareManager::scanDirectory(const string &path)
{
    if (!FileUtils::isDir(path))
    {
        cWarningDom(TAG) << "Firmware directory does not exist: " << path;
        return;
    }

    DIR *dir = opendir(path.c_str());
    if (!dir)
    {
        cWarningDom(TAG) << "Failed to open firmware directory: " << path;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        string name = entry->d_name;

        // Skip . and ..
        if (name == "." || name == "..")
            continue;

        string subPath = path + "/" + name;

        // Only process directories
        if (!FileUtils::isDir(subPath))
            continue;

        // Look for manifest.json in this directory
        string manifestPath = subPath + "/manifest.json";
        if (!FileUtils::exists(manifestPath))
        {
            cDebugDom(TAG) << "No manifest.json in: " << subPath;
            continue;
        }

        FirmwareManifest manifest;
        if (!manifest.loadFromFile(manifestPath))
        {
            cWarningDom(TAG) << "Failed to load manifest from: " << manifestPath;
            continue;
        }

        if (!manifest.isValid())
        {
            cWarningDom(TAG) << "Invalid manifest in: " << manifestPath;
            continue;
        }

        // Validate that directory name matches hardware_id
        if (name != manifest.getHardwareId())
        {
            cWarningDom(TAG) << "Directory name '" << name
                             << "' does not match hardware_id '"
                             << manifest.getHardwareId() << "'";
            continue;
        }

        // Verify firmware file checksum
        if (!manifest.verifyChecksum())
        {
            cWarningDom(TAG) << "Checksum verification failed for: " << manifest.getHardwareId();
            continue;
        }

        // Add to cache
        firmwareCache[manifest.getHardwareId()] = manifest;
        cDebugDom(TAG) << "Loaded firmware: " << manifest.getHardwareId()
                       << " v" << manifest.getVersion();
    }

    closedir(dir);
}

const FirmwareManifest *OtaFirmwareManager::getFirmware(const string &hardwareId) const
{
    auto it = firmwareCache.find(hardwareId);
    if (it != firmwareCache.end())
        return &it->second;
    return nullptr;
}

Json OtaFirmwareManager::buildOffer(const string &hardwareId, const string &currentVersion) const
{
    if (hardwareId.empty())
    {
        cDebugDom(TAG) << "Device has no hardware_id, skipping OTA check";
        return Json();
    }

    const FirmwareManifest *firmware = getFirmware(hardwareId);
    if (!firmware)
    {
        cDebugDom(TAG) << "No firmware available for hardware_id: " << hardwareId;
        return Json();
    }

    // Simple string comparison: if versions differ, offer update
    // This allows switching between dev and release branches
    if (firmware->getVersion() == currentVersion)
    {
        cDebugDom(TAG) << "Device " << hardwareId << " is up to date (v" << currentVersion << ")";
        return Json();
    }

    Json data;
    data["hardware_id"] = firmware->getHardwareId();
    data["version"] = firmware->getVersion();
    data["checksum_sha256"] = firmware->getChecksumSha256();
    data["download_url"] = "/api/v3/ota/firmware/" + firmware->getHardwareId() + "/download";

    if (!firmware->getReleaseNotes().empty())
        data["release_notes"] = firmware->getReleaseNotes();

    if (!firmware->getName().empty())
        data["name"] = firmware->getName();

    return data;
}

bool OtaFirmwareManager::checkDeviceForUpdate(RemoteUIWebSocketHandler *handler,
                                              const string &hardwareId,
                                              const string &currentVersion)
{
    if (!enabled || !handler)
        return false;

    Json data = buildOffer(hardwareId, currentVersion);
    if (data.is_null())
        return false;

    if (!offersAllowed())
    {
        cInfoDom(TAG) << "Firmware update for " << hardwareId << " held for "
                      << offerGate.msUntilAllowed(nowMs()) / 1000 << " s, server not settled";
        scheduleDeferredNotify();
        return false;
    }

    cInfoDom(TAG) << "Firmware update available for " << hardwareId
                  << ": device has v" << currentVersion
                  << ", available v" << data["version"].get<string>();

    handler->sendJson("remote_ui_fw_update_available", data);

    cInfoDom(TAG) << "Sent firmware update notification for " << hardwareId;
    return true;
}

void OtaFirmwareManager::notifyAllDevices()
{
    if (!enabled)
        return;

    cInfoDom(TAG) << "Notifying all connected devices about firmware updates"
                  << " (connected: " << RemoteUIManager::Instance().getConnectedHandlerCount() << ")";

    // Delegate to RemoteUIManager which has access to WebSocket handlers
    RemoteUIManager::Instance().notifyOtaUpdates();
}
