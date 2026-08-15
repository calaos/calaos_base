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
#ifndef S_IPCam_H
#define S_IPCam_H

#include "IOBase.h"
#include "UrlDownloader.h"

namespace Calaos
{

class IPCam: public IOBase
{
protected:
    Params caps;

    string lastSnapshot;
    std::function<void(const string &)> snapshotDataCb = {};
    UrlDownloader *cameraSnapDl = nullptr;

public:
    IPCam(Params &p);
    virtual ~IPCam();

    //Standard IPCam functions.
    virtual std::string getVideoUrl() { return ""; } //return the mjpeg url stream
    virtual std::string getPictureUrl() { return ""; } //return the url for a single frame

    //Capabilities
    /*************************************************
     * List of capabilities:
     * ptz : bool
     * position : int (number of memory position. if 0, position is not available)
     * resolution : string (list of resolution, space separated)
     * led : bool (to activate leds)
     * buzzer : bool (to activate buzzer)
     * privacy: bool (to activate privacy mode)
     * quality: int (range for quality level)
     * brightness: int (range)
     * contrast: int (range)
     * color: int (range)
     * saturation: int (range)
     * sharpness: int (range)
     * hue: int (range)
     **************************************************/
    virtual Params getCapabilities() { return caps; }
    virtual void activateCapabilities(std::string capability, std::string cmd, std::string value) { }

    virtual DATA_TYPE get_type() { return TSTRING; }

    virtual bool set_value(std::string val);

    virtual void downloadSnapshot(std::function<void(const string &)> dataCb);

    //T2.19: effective TLS policy of this camera. insecure param defaults to
    //true (self-signed HTTPS cameras, grandfathered configs), insecure="false"
    //is the per-device hardening opt-in. EVERY transfer to a camera URL
    //(snapshot, PTZ, mjpeg relay, rule attachment download) must honor it.
    bool tlsInsecure() { return UrlDownloader::insecureParamEnabled(get_param("insecure")); }

    //T2.19: fire-and-forget GET to a camera URL honoring the per-device
    //insecure param (replaces the unconditional UrlDownloader::insecureGet
    //calls of the camera drivers)
    void camGet(const string &url)
    {
        if (tlsInsecure())
            UrlDownloader::insecureGet(url);
        else
            UrlDownloader::get(url);
    }

    //T3.3: mask credentials embedded in a camera URL so it can be logged
    //safely. Handles userinfo passwords (http://user:secret@host/...) and the
    //values of known credential query parameters (Foscam usr/pwd, Synology
    //account/passwd/_sid, generic user/username/password/loginuse/loginpas).
    //Any URL a camera driver hands to a log statement must go through this.
    static std::string maskUrlCredentials(const std::string &url);

    virtual bool SaveToXml(TiXmlElement *node);
};

}

#endif
