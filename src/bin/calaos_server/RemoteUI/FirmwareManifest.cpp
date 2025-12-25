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
#include "FirmwareManifest.h"
#include "FileUtils.h"
#include <fstream>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>

static const char *TAG = "ota";

using namespace Calaos;

bool FirmwareManifest::loadFromFile(const string &manifestPath)
{
    if (!FileUtils::exists(manifestPath))
    {
        cWarningDom(TAG) << "Manifest file does not exist: " << manifestPath;
        return false;
    }

    // Store the directory containing the manifest
    size_t lastSlash = manifestPath.find_last_of('/');
    if (lastSlash != string::npos)
        directory = manifestPath.substr(0, lastSlash);
    else
        directory = ".";

    try
    {
        std::ifstream file(manifestPath);
        if (!file.is_open())
        {
            cWarningDom(TAG) << "Failed to open manifest file: " << manifestPath;
            return false;
        }

        Json manifest = Json::parse(file);

        // Parse required fields
        if (!manifest.contains("schema_version") || !manifest["schema_version"].is_number_integer())
        {
            cWarningDom(TAG) << "Missing or invalid schema_version in " << manifestPath;
            return false;
        }
        schemaVersion = manifest["schema_version"];

        if (!manifest.contains("version") || !manifest["version"].is_string())
        {
            cWarningDom(TAG) << "Missing or invalid version in " << manifestPath;
            return false;
        }
        version = manifest["version"];

        if (!manifest.contains("hardware_id") || !manifest["hardware_id"].is_string())
        {
            cWarningDom(TAG) << "Missing or invalid hardware_id in " << manifestPath;
            return false;
        }
        hardwareId = manifest["hardware_id"];

        if (!manifest.contains("firmware_file") || !manifest["firmware_file"].is_string())
        {
            cWarningDom(TAG) << "Missing or invalid firmware_file in " << manifestPath;
            return false;
        }
        firmwareFile = manifest["firmware_file"];

        if (!manifest.contains("checksum_sha256") || !manifest["checksum_sha256"].is_string())
        {
            cWarningDom(TAG) << "Missing or invalid checksum_sha256 in " << manifestPath;
            return false;
        }
        checksumSha256 = manifest["checksum_sha256"];

        // Parse optional fields
        if (manifest.contains("name") && manifest["name"].is_string())
            name = manifest["name"];

        if (manifest.contains("description") && manifest["description"].is_string())
            description = manifest["description"];

        if (manifest.contains("release_date") && manifest["release_date"].is_string())
            releaseDate = manifest["release_date"];

        if (manifest.contains("release_notes") && manifest["release_notes"].is_string())
            releaseNotes = manifest["release_notes"];

        if (manifest.contains("metadata") && manifest["metadata"].is_object())
            metadata = manifest["metadata"];

        cDebugDom(TAG) << "Loaded manifest for " << hardwareId << " v" << version;
        return true;
    }
    catch (const std::exception &e)
    {
        cWarningDom(TAG) << "Failed to parse manifest " << manifestPath << ": " << e.what();
        return false;
    }
}

bool FirmwareManifest::isValid() const
{
    return schemaVersion > 0 &&
           !version.empty() &&
           !hardwareId.empty() &&
           !firmwareFile.empty() &&
           !checksumSha256.empty();
}

string FirmwareManifest::getFirmwarePath() const
{
    if (directory.empty())
        return firmwareFile;
    return directory + "/" + firmwareFile;
}

bool FirmwareManifest::verifyChecksum() const
{
    string firmwarePath = getFirmwarePath();
    if (!FileUtils::exists(firmwarePath))
    {
        cWarningDom(TAG) << "Firmware file does not exist: " << firmwarePath;
        return false;
    }

    string computed = computeFileSha256(firmwarePath);
    if (computed.empty())
    {
        cWarningDom(TAG) << "Failed to compute checksum for: " << firmwarePath;
        return false;
    }

    // Case-insensitive comparison
    string expectedLower = Utils::str_to_lower(checksumSha256);
    string computedLower = Utils::str_to_lower(computed);

    if (expectedLower != computedLower)
    {
        cWarningDom(TAG) << "Checksum mismatch for " << firmwarePath
                         << ": expected " << checksumSha256
                         << ", got " << computed;
        return false;
    }

    cDebugDom(TAG) << "Checksum verified for " << hardwareId << " v" << version;
    return true;
}

Json FirmwareManifest::toJson() const
{
    Json j;
    j["schema_version"] = schemaVersion;
    j["version"] = version;
    j["hardware_id"] = hardwareId;
    j["firmware_file"] = firmwareFile;
    j["checksum_sha256"] = checksumSha256;

    if (!name.empty())
        j["name"] = name;
    if (!description.empty())
        j["description"] = description;
    if (!releaseDate.empty())
        j["release_date"] = releaseDate;
    if (!releaseNotes.empty())
        j["release_notes"] = releaseNotes;
    if (!metadata.empty())
        j["metadata"] = metadata;

    return j;
}

string FirmwareManifest::computeFileSha256(const string &filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
        return "";

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
        return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
    {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1)
        {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);

    return oss.str();
}
