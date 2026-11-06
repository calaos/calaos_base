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
 **  You should have received a copy of the GNU General Public License
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/* T1.10 — RemoteUI WebSocket/OTA lifecycle regression tests.
 *
 * Covers the two pure helpers introduced by the ticket:
 *  - RemoteUIWebSocketHandler::parseGridDimension: replaces a raw std::stoi
 *    on client-supplied grid_h/grid_w inside a timer callback. std::stoi
 *    throws std::invalid_argument/std::out_of_range which would escape into
 *    the libuv event loop and take the server down.
 *  - OtaFirmwareManager::computeRescanIntervalMs: replaces the naive
 *    `int * 60 * 1000` multiply which overflows (UB) above ~35791 minutes,
 *    producing a wrong (possibly tiny) rescan timer period.
 *
 * The lifecycle (use-after-free) fixes of the ticket — post-auth
 * Timer::singleShot and buildJsonState completion guarded by an alive
 * token — cannot be unit-tested without a running event loop and a full
 * WebSocket stack; they are covered by code review + ASan scenarios.
 */

#include <gtest/gtest.h>
#include <climits>
#include <cstdint>
#include <string>

#include "RemoteUI/RemoteUIWebSocketHandler.h"
#include "RemoteUI/OtaFirmwareManager.h"

using Calaos::RemoteUIWebSocketHandler;
using Calaos::OtaFirmwareManager;

//---------------------------------------------------------------------------
// parseGridDimension
//---------------------------------------------------------------------------

TEST(ParseGridDimension, ValidValues)
{
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("3", 7));
    EXPECT_EQ(12, RemoteUIWebSocketHandler::parseGridDimension("12", 7));
    EXPECT_EQ(1, RemoteUIWebSocketHandler::parseGridDimension("1", 7));
    EXPECT_EQ(1000, RemoteUIWebSocketHandler::parseGridDimension("1000", 7));
}

TEST(ParseGridDimension, MalformedValuesFallBackWithoutThrowing)
{
    // Every one of these made std::stoi throw out of the timer callback
    EXPECT_NO_THROW({
        EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("", 3));
        EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("abc", 3));
        EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("--", 3));
        EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("99999999999999999999", 3));
    });
}

TEST(ParseGridDimension, TrailingGarbageRejected)
{
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("12abc", 3));
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("4 4", 3));
}

TEST(ParseGridDimension, OutOfRangeValuesFallBack)
{
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("0", 3));
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("-5", 3));
    EXPECT_EQ(3, RemoteUIWebSocketHandler::parseGridDimension("1001", 3));
}

//---------------------------------------------------------------------------
// computeRescanIntervalMs
//---------------------------------------------------------------------------

TEST(ComputeRescanIntervalMs, NominalValues)
{
    EXPECT_EQ(60000ull, OtaFirmwareManager::computeRescanIntervalMs(1));
    EXPECT_EQ(3600000ull, OtaFirmwareManager::computeRescanIntervalMs(60));
    EXPECT_EQ(86400000ull, OtaFirmwareManager::computeRescanIntervalMs(1440));
}

TEST(ComputeRescanIntervalMs, MinimumClamped)
{
    EXPECT_EQ(60000ull, OtaFirmwareManager::computeRescanIntervalMs(0));
    EXPECT_EQ(60000ull, OtaFirmwareManager::computeRescanIntervalMs(-42));
}

TEST(ComputeRescanIntervalMs, NoSignedOverflowAboveInt32Boundary)
{
    // 35791 minutes is the last value that fits in a signed 32-bit multiply;
    // the old `int * 60 * 1000` was UB from 35792 on
    EXPECT_EQ(35791ull * 60 * 1000, OtaFirmwareManager::computeRescanIntervalMs(35791));
    EXPECT_EQ(35792ull * 60 * 1000, OtaFirmwareManager::computeRescanIntervalMs(35792));
    EXPECT_EQ(40000ull * 60 * 1000, OtaFirmwareManager::computeRescanIntervalMs(40000));
}

TEST(ComputeRescanIntervalMs, MaximumClampedTo30Days)
{
    const uint64_t maxMs = 30ull * 24 * 60 * 60 * 1000;
    EXPECT_EQ(maxMs, OtaFirmwareManager::computeRescanIntervalMs(30 * 24 * 60));
    EXPECT_EQ(maxMs, OtaFirmwareManager::computeRescanIntervalMs(30 * 24 * 60 + 1));
    EXPECT_EQ(maxMs, OtaFirmwareManager::computeRescanIntervalMs(INT_MAX));
}

TEST(ComputeRescanIntervalMs, NeverZero)
{
    for (int m : {-1, 0, 1, 59, 60, 35792, INT_MAX})
        EXPECT_GT(OtaFirmwareManager::computeRescanIntervalMs(m), 0ull) << "minutes=" << m;
}
