/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 ******************************************************************************/

// T3.3 — IPCam cleanup + Synology snapshot fix.
//
// - IPCam::maskUrlCredentials: cameras embed credentials either as userinfo
//   (Axis/Planet: http://user:pass@host/) or as query parameters (Foscam
//   usr/pwd, Synology account/passwd/_sid). Any URL that reaches a log
//   statement must be masked first.
// - SynoSurveillanceStation::snapshotPayload: pure decision helper extracted
//   from getSnapshot(). Regression: the old inline check for a non-jpeg body
//   was missing a return after cb({}), so the completion callback fired twice
//   (once empty, once with the JSON error body). The helper maps every
//   response to exactly one payload, and the lambda now has a single cb()
//   call site.

#include <gtest/gtest.h>

#include <string>

#include "IPCam.h"
#include "SynoSurveillanceStation.h"

using namespace Calaos;

namespace
{

// ---------------------------------------------------------------- masking

TEST(MaskUrlCredentials, FoscamQueryParamsAreMasked)
{
    // Foscam.cpp getPictureUrl() shape (spec sites :50/:61)
    EXPECT_EQ("http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=snapPicture2&usr=*****&pwd=*****",
              IPCam::maskUrlCredentials(
                  "http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=snapPicture2&usr=admin&pwd=s3cret"));
}

TEST(MaskUrlCredentials, FoscamPtzAndStopUrlsAreMasked)
{
    // Foscam.cpp activateCapabilities() shapes (spec sites :97/:107)
    EXPECT_EQ("http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=ptzMoveUp&usr=*****&pwd=*****",
              IPCam::maskUrlCredentials(
                  "http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=ptzMoveUp&usr=bob&pwd=hunter2"));
    EXPECT_EQ("http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=ptzStopRun&usr=*****&pwd=*****",
              IPCam::maskUrlCredentials(
                  "http://cam:88/cgi-bin/CGIProxy.fcgi?cmd=ptzStopRun&usr=bob&pwd=hunter2"));
}

TEST(MaskUrlCredentials, UserinfoPasswordIsMasked)
{
    // Axis/Planet shape: user kept (useful for debugging), password masked
    EXPECT_EQ("http://admin:*****@10.0.0.1:80/axis-cgi/jpg/image.cgi?quality=30&camera=1",
              IPCam::maskUrlCredentials(
                  "http://admin:s3cret@10.0.0.1:80/axis-cgi/jpg/image.cgi?quality=30&camera=1"));
}

TEST(MaskUrlCredentials, SynoLoginParamsAreMasked)
{
    EXPECT_EQ("https://nas:5001/webapi/auth.cgi?api=SYNO.API.Auth&method=Login"
              "&version=6&format=2&account=*****&passwd=*****",
              IPCam::maskUrlCredentials(
                  "https://nas:5001/webapi/auth.cgi?api=SYNO.API.Auth&method=Login"
                  "&version=6&format=2&account=bob&passwd=hunter2"));
}

TEST(MaskUrlCredentials, SynoSessionIdIsMasked)
{
    EXPECT_EQ("https://nas:5001/webapi/snap.cgi?id=3&_sid=*****&profileType=1",
              IPCam::maskUrlCredentials(
                  "https://nas:5001/webapi/snap.cgi?id=3&_sid=AbCdEf123&profileType=1"));
}

TEST(MaskUrlCredentials, KeyMatchIsCaseInsensitive)
{
    EXPECT_EQ("http://cam/x?PWD=*****&Usr=*****",
              IPCam::maskUrlCredentials("http://cam/x?PWD=a&Usr=b"));
}

TEST(MaskUrlCredentials, UrlWithoutCredentialsIsUntouched)
{
    const std::string plain = "http://10.0.0.2:80/Jpeg/CamImg.jpg";
    EXPECT_EQ(plain, IPCam::maskUrlCredentials(plain));

    const std::string query = "http://cam/GetData.cgi?resolution=640x480&camera=1";
    EXPECT_EQ(query, IPCam::maskUrlCredentials(query));
}

TEST(MaskUrlCredentials, HostPortColonIsNotAPassword)
{
    // colon in authority without an '@' must not be treated as userinfo
    const std::string url = "http://10.0.0.1:8080/path?x=1";
    EXPECT_EQ(url, IPCam::maskUrlCredentials(url));
}

TEST(MaskUrlCredentials, AtSignInQueryIsNotUserinfo)
{
    // '@' after the authority (in the query) must not trigger userinfo masking
    const std::string url = "http://cam/notify?email=a:b@c.org";
    EXPECT_EQ(url, IPCam::maskUrlCredentials(url));
}

TEST(MaskUrlCredentials, UserinfoWithoutPasswordIsUntouched)
{
    const std::string url = "http://admin@cam/image.cgi";
    EXPECT_EQ(url, IPCam::maskUrlCredentials(url));
}

TEST(MaskUrlCredentials, ValuelessTokenAndEmptyValueSurvive)
{
    EXPECT_EQ("http://cam/x?foo&pwd=*****&bar=",
              IPCam::maskUrlCredentials("http://cam/x?foo&pwd=x&bar="));
}

TEST(MaskUrlCredentials, EmptyAndNonUrlInputsPassThrough)
{
    EXPECT_EQ("", IPCam::maskUrlCredentials(""));
    EXPECT_EQ("not a url", IPCam::maskUrlCredentials("not a url"));
}

// ------------------------------------------------- Syno snapshot decision

TEST(SynoSnapshotPayload, JpegBodyOn200IsAccepted)
{
    EXPECT_EQ("\xFF\xD8jpegdata",
              SynoSurveillanceStation::snapshotPayload(200, "image/jpeg",
                                                       "\xFF\xD8jpegdata"));
}

TEST(SynoSnapshotPayload, JsonErrorBodyOn200IsRejected)
{
    // Regression: this exact case used to fire the completion callback twice
    // (cb({}) then cb(data)) because the non-jpeg branch had no return.
    EXPECT_EQ("", SynoSurveillanceStation::snapshotPayload(
                      200, "application/json; charset=UTF-8",
                      "{\"error\":{\"code\":105},\"success\":false}"));
}

TEST(SynoSnapshotPayload, HttpErrorIsRejectedWhateverTheBody)
{
    EXPECT_EQ("", SynoSurveillanceStation::snapshotPayload(500, "image/jpeg", "x"));
    EXPECT_EQ("", SynoSurveillanceStation::snapshotPayload(404, "", ""));
}

TEST(SynoSnapshotPayload, MissingContentTypeOn200IsRejected)
{
    EXPECT_EQ("", SynoSurveillanceStation::snapshotPayload(200, "", "data"));
}

} //namespace
