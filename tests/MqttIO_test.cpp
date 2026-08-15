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

// T3.2c — Mqtt thin IO subclasses: MqttOutputLightRGB color+state fix.
//
// Before T3.2c, MqttOutputLightRGB::readValue() was dead code behind a
// blocking TODO ("it does not work for now"): the broker feedback was read
// then thrown away, so external color changes never reached calaos, while
// setColor() emitted events unconditionally without any confirmation.
//
// The fix routes the feedback through OutputLightRGB::stateUpdated() with
// useRealState = true (same pattern as MqttOutputLight), and the on/off +
// color decision is extracted into the pure, header-inline
// MqttOutputLightRGB::stateFromColor() tested here. The MQTT payload carries
// no separate on/off state (only path_x/path_y/path_brightness) and
// setColorReal() publishes black for OFF, hence the two invariants:
//   - black feedback means OFF,
//   - black feedback must NOT overwrite the last known color, otherwise
//     a later set_value("on") would re-send black and the light could
//     never be turned back on.
//
// A true red-before-green test at the IO level is not achievable here:
// instantiating any Mqtt IO goes through MqttBrokersList/MqttCtrl, which
// spawns the calaos_mqtt external process and needs a live broker. Only the
// extracted decision logic is testable without linking the Mqtt objects.

#include <gtest/gtest.h>

#include "MqttOutputLightRGB.h"

using namespace Calaos;

namespace
{

const ColorValue black(0, 0, 0);

TEST(MqttRgbStateFromColor, NonBlackFeedbackTurnsOnWithIncomingColor)
{
    ColorValue current(10, 20, 30);
    ColorValue incoming(200, 100, 50);

    auto [color, state] = MqttOutputLightRGB::stateFromColor(incoming, current);

    EXPECT_TRUE(state);
    EXPECT_TRUE(color == incoming);
}

TEST(MqttRgbStateFromColor, BlackFeedbackTurnsOff)
{
    ColorValue current(200, 100, 50);

    auto [color, state] = MqttOutputLightRGB::stateFromColor(black, current);

    EXPECT_FALSE(state);
    EXPECT_TRUE(color == current);
}

TEST(MqttRgbStateFromColor, BlackFeedbackKeepsLastKnownColor)
{
    //Regression guard: storing black on OFF would make set_value("on")
    //re-publish black, and the light could never be turned back on.
    ColorValue current(200, 100, 50);

    auto [color, state] = MqttOutputLightRGB::stateFromColor(black, current);

    EXPECT_FALSE(state);
    EXPECT_TRUE(color == current);
}

TEST(MqttRgbStateFromColor, AlmostBlackIsStillOn)
{
    //Only true black means OFF: a very dim color is a color
    ColorValue dim(0, 0, 1);

    auto [color, state] = MqttOutputLightRGB::stateFromColor(dim, black);

    EXPECT_TRUE(state);
    EXPECT_TRUE(color == dim);
}

TEST(MqttRgbStateFromColor, OffOnSequenceRestoresColor)
{
    //Full sequence: light red -> feedback black (off) -> feedback red (on).
    //The stored color must survive the OFF phase.
    ColorValue stored(255, 0, 0);

    auto [afterOffColor, afterOffState] =
        MqttOutputLightRGB::stateFromColor(black, stored);
    EXPECT_FALSE(afterOffState);

    auto [afterOnColor, afterOnState] =
        MqttOutputLightRGB::stateFromColor(ColorValue(255, 0, 0), afterOffColor);
    EXPECT_TRUE(afterOnState);
    EXPECT_TRUE(afterOnColor == stored);
}

} //namespace
