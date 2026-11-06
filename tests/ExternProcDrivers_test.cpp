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

// T1.17 — Extern-proc driver mains regression tests.
//
// - OWFSUtils: the OWFS scan filter used to drop every device whose address
//   starts with a boundary hex character ('0', '9', 'A' or 'F') because of
//   strict >/< comparisons.
// - WagoMap::respawnDelay: the subprocess auto-restart used to respawn
//   immediately in a tight loop; the backoff delays must start small, grow
//   and stay capped.

#include <gtest/gtest.h>

#include "OWFSUtils.h"
#include "WagoMap.h"

//----------------------------------------------------------------------------
// OWFS scan filter
//----------------------------------------------------------------------------

TEST(OwfsScanFilter, KeepsRegularDeviceEntries)
{
    EXPECT_TRUE(OWFSUtils::entryIsDevice("28.4515A80000/"));
    EXPECT_TRUE(OWFSUtils::entryIsDevice("10.67C6697351FF"));
    EXPECT_TRUE(OWFSUtils::entryIsDevice("26.8AC0B2D716C1/"));
}

TEST(OwfsScanFilter, KeepsBoundaryHexFamilyCodes)
{
    //Regression: these were dropped by the old strict >/< comparisons
    EXPECT_TRUE(OWFSUtils::entryIsDevice("05.4AEC29CDBAAB/")); //starts with '0'
    EXPECT_TRUE(OWFSUtils::entryIsDevice("9D.65B4C7AF8E12/")); //starts with '9'
    EXPECT_TRUE(OWFSUtils::entryIsDevice("A6.D28E1D9F1A5C/")); //starts with 'A'
    EXPECT_TRUE(OWFSUtils::entryIsDevice("FF.062B27C99B41/")); //starts with 'F'
}

TEST(OwfsScanFilter, RejectsOwfsVirtualFolders)
{
    //OWFS virtual folders are lowercase: they must not be reported as
    //devices even though 'a'..'f' are hex digits (this is why the filter
    //cannot simply use isxdigit())
    EXPECT_FALSE(OWFSUtils::entryIsDevice("alarm/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("bus.0/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("settings/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("simultaneous/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("statistics/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("structure/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("system/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("uncached/"));
}

TEST(OwfsScanFilter, RejectsEmptyAndNonHexEntries)
{
    EXPECT_FALSE(OWFSUtils::entryIsDevice(""));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("G5.000000000000/"));
    EXPECT_FALSE(OWFSUtils::entryIsDevice("/"));
}

TEST(OwfsScanFilter, DeviceNameNormalization)
{
    EXPECT_EQ("28.4515A80000", OWFSUtils::entryToDeviceName("28.4515A80000/"));
    EXPECT_EQ("28.4515A80000", OWFSUtils::entryToDeviceName("28.4515A80000"));
    EXPECT_EQ("", OWFSUtils::entryToDeviceName("/"));
    EXPECT_EQ("", OWFSUtils::entryToDeviceName(""));
}

//----------------------------------------------------------------------------
// Wago subprocess respawn backoff
//----------------------------------------------------------------------------

using Calaos::WagoMap;

TEST(WagoRespawnBackoff, FirstDelayIsNotATightLoop)
{
    EXPECT_GE(WagoMap::respawnDelay(0), 1.0);
}

TEST(WagoRespawnBackoff, DelaysGrowMonotonically)
{
    for (int i = 1;i < 20;i++)
        EXPECT_GE(WagoMap::respawnDelay(i), WagoMap::respawnDelay(i - 1))
            << "delay must not shrink at attempt " << i;

    //it actually backs off (grows) at the beginning
    EXPECT_GT(WagoMap::respawnDelay(1), WagoMap::respawnDelay(0));
}

TEST(WagoRespawnBackoff, DelayCapIsLowForFastRecovery)
{
    //The Wago is the centerpiece of the installation: recovery after a
    //maintenance network cut must never wait out a long backoff. Cap at 5s.
    for (int i = 0;i < 100;i++)
        EXPECT_LE(WagoMap::respawnDelay(i), 5.0);
}

TEST(WagoRespawnBackoff, NeverGivesUp)
{
    //No retry limit: the delay saturates at the cap and stays there forever
    EXPECT_DOUBLE_EQ(WagoMap::respawnDelay(1000), WagoMap::respawnDelay(10));
    EXPECT_GT(WagoMap::respawnDelay(1000000), 0.0);
}

TEST(WagoRespawnBackoff, OutOfRangeAttemptsAreSafe)
{
    EXPECT_DOUBLE_EQ(WagoMap::respawnDelay(-1), WagoMap::respawnDelay(0));
}

TEST(WagoRespawnBackoff, FailureLoggingPeriodIsSane)
{
    EXPECT_GT(WagoMap::RESPAWN_LOG_EVERY, 0);
    EXPECT_LE(WagoMap::RESPAWN_LOG_EVERY, 100);
}
