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
#ifndef FIRMWAREMANIFEST_H
#define FIRMWAREMANIFEST_H

#include "Utils.h"
#include "json.hpp"

using Json = nlohmann::json;
using namespace Utils;

namespace Calaos
{

class FirmwareManifest
{
public:
    FirmwareManifest() = default;
    ~FirmwareManifest() = default;

    // Parse manifest from JSON file
    bool loadFromFile(const string &manifestPath);

    // Validate all required fields are present
    bool isValid() const;

    // Verify SHA256 checksum of the firmware file
    bool verifyChecksum() const;

    // Get the full path to the firmware binary file
    string getFirmwarePath() const;

    // Serialize to JSON for API responses
    Json toJson() const;

    // Getters for manifest fields
    int getSchemaVersion() const { return schemaVersion; }
    const string &getName() const { return name; }
    const string &getDescription() const { return description; }
    const string &getVersion() const { return version; }
    const string &getHardwareId() const { return hardwareId; }
    const string &getFirmwareFile() const { return firmwareFile; }
    const string &getChecksumSha256() const { return checksumSha256; }
    const string &getReleaseDate() const { return releaseDate; }
    const string &getReleaseNotes() const { return releaseNotes; }
    const Json &getMetadata() const { return metadata; }

    // Get directory containing this manifest
    const string &getDirectory() const { return directory; }

private:
    // Required fields
    int schemaVersion = 0;
    string version;
    string hardwareId;
    string firmwareFile;
    string checksumSha256;

    // Optional fields
    string name;
    string description;
    string releaseDate;
    string releaseNotes;
    Json metadata;

    // Internal
    string directory;  // Directory containing the manifest

    // Helper to compute SHA256 of a file
    static string computeFileSha256(const string &filePath);
};

}

#endif
