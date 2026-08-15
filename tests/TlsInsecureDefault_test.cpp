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

// T2.19 — TLS insecure by DEFAULT for user-configured URLs (user decision,
// supersedes the T2.17 default for those paths).
//
// Policy pinned here:
// - GRANDFATHERING: a pre-existing config with NO `insecure` param anywhere
//   must behave exactly like pre-T2.17: every device transfer skips TLS
//   certificate verification. UrlDownloader::insecureParamEnabled maps an
//   absent/empty param to "insecure", and every user-URL consumer (IPCam
//   snapshot/PTZ/mjpeg relay/ActionCameraDownload, Hue, Web IOs through
//   WebCtrl, Syno) routes its `insecure` param through it.
// - insecure="false" is the per-device hardening opt-in: ONLY the exact
//   string "false" turns verification back on (bounded, non-throwing —
//   garbage can never accidentally harden or weaken differently from the
//   documented default).
// - The library default of UrlDownloader itself stays "verified" (T2.17):
//   the hardcoded calaos.fr service consumers (NotifManager push) construct
//   a plain downloader and never call the param helpers.

#include <gtest/gtest.h>

#include <string>

#include "UrlDownloader.h"

// ------------------------------------------------ param -> policy mapping

TEST(InsecureParam, AbsentParamMeansInsecureGrandfathered)
{
    //get_param() of a missing key returns "": every existing installation
    //(no insecure param anywhere) must stay on pre-T2.17 behavior
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled(""));
}

TEST(InsecureParam, ExplicitTrueMeansInsecure)
{
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("true"));
}

TEST(InsecureParam, OnlyExactFalseHardens)
{
    EXPECT_FALSE(UrlDownloader::insecureParamEnabled("false"));

    //bounded, non-throwing: anything that is not exactly "false" keeps the
    //documented default (insecure), never an accidental third behavior
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("False"));
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("FALSE"));
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("0"));
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("no"));
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled(" false"));
    EXPECT_TRUE(UrlDownloader::insecureParamEnabled("garbage"));
}

// ------------------------------------------- applied to a real downloader

TEST(InsecureParam, GrandfatheredDeviceTransferIsInsecure)
{
    //the exact call path of every per-device consumer with no param set
    UrlDownloader dl("https://camera.local/snapshot.jpg", false);
    dl.setInsecureFromParam("");
    EXPECT_TRUE(dl.isInsecure());
}

TEST(InsecureParam, InsecureFalseGivesVerifiedTransfer)
{
    UrlDownloader dl("https://camera.local/snapshot.jpg", false);
    dl.setInsecureFromParam("false");
    EXPECT_FALSE(dl.isInsecure()) << "insecure=\"false\" must harden the device";
}

TEST(InsecureParam, InsecureTrueGivesInsecureTransfer)
{
    UrlDownloader dl("https://camera.local/snapshot.jpg", false);
    dl.setInsecureFromParam("true");
    EXPECT_TRUE(dl.isInsecure());
}

// ----------------------------------------- hardcoded calaos.fr consumers

TEST(InsecureParam, PlainDownloaderStaysVerifiedForHardcodedServices)
{
    //NotifManager (https://push.calaos.fr/api/push) and any other hardcoded
    //calaos.fr service build a plain downloader and never touch the param
    //helpers: the T2.17 library default (verified) still protects them
    UrlDownloader dl("https://push.calaos.fr/api/push", false);
    EXPECT_FALSE(dl.isInsecure());
}
