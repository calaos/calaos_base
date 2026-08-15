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

// T1.19 — IO controllers (Gpio/Web) regression tests (trimmed by T2.12).
//
// - WebCtrl::getValueText: an empty or delimiter-only path used to index
//   tokens[0] on an empty vector (Utils::split with max=0 does not pad).
//
// The GpioCtrl destructor fd-leak fix (inverted close() guard) is not unit
// tested here: the fd is only ever opened through the hardcoded
// /sys/class/gpio/... paths, which do not exist off-target.
//
// T2.13 — GpioCtrl::parseDebounceTime/debounceTimeValid: the debounce_time
// config value is now honored, with a bounded non-throwing parse (fallback
// 0.05 s). Helpers are header-inline, no GpioCtrl.o linked here.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "GpioCtrl.h"
#include "WebCtrl.h"

using namespace Calaos;

namespace
{

class WebCtrlTextTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        filename = "/tmp/webctrl_t119_test.txt";
        std::ofstream f(filename);
        f << "10.0,10.1,10.2,10.3\n";
        f << "20.0,20.1,20.2,20.3\n";
        f.close();
    }

    void TearDown() override
    {
        ::remove(filename.c_str());
    }

    std::string filename;
    WebCtrl ctrl; //default constructed, enough for getValueText()
};

TEST_F(WebCtrlTextTest, EmptyPathReturnsEmptyValue)
{
    //Regression: tokens[0] was indexed without a non-empty check
    EXPECT_EQ("", ctrl.getValueText("", filename));
}

TEST_F(WebCtrlTextTest, DelimiterOnlyPathReturnsEmptyValue)
{
    //split() skips leading delimiters: "///" yields an empty token vector
    EXPECT_EQ("", ctrl.getValueText("///", filename));
}

TEST_F(WebCtrlTextTest, NominalPathReturnsToken)
{
    //4th token of the 2nd line, ',' separated (documented example)
    EXPECT_EQ("20.3", ctrl.getValueText("2/4/,", filename));
}

TEST_F(WebCtrlTextTest, LineOnlyPathReturnsWholeLine)
{
    EXPECT_EQ("10.0,10.1,10.2,10.3", ctrl.getValueText("1", filename));
}

TEST_F(WebCtrlTextTest, NonNumericLineNumberReturnsEmptyValue)
{
    //line_nb must stay at a sane default when conversion fails
    EXPECT_EQ("", ctrl.getValueText("abc/2/,", filename));
}

TEST_F(WebCtrlTextTest, MissingFileReturnsEmptyValue)
{
    EXPECT_EQ("", ctrl.getValueText("1/1/,", "/nonexistent/calaos_t119"));
}

TEST(GpioDebounceTimeTest, NominalValueIsParsed)
{
    EXPECT_DOUBLE_EQ(0.2, GpioCtrl::parseDebounceTime("0.2"));
    EXPECT_DOUBLE_EQ(1.0, GpioCtrl::parseDebounceTime("1"));
}

TEST(GpioDebounceTimeTest, DefaultValueRoundTrips)
{
    EXPECT_DOUBLE_EQ(GpioCtrl::DEBOUNCE_TIME_DEFAULT,
                     GpioCtrl::parseDebounceTime("0.05"));
}

TEST(GpioDebounceTimeTest, UpperBoundIsInclusive)
{
    EXPECT_DOUBLE_EQ(5.0, GpioCtrl::parseDebounceTime("5.0"));
}

TEST(GpioDebounceTimeTest, EmptyValueFallsBack)
{
    //Regression guard for the Utils::from_string("") == true footgun:
    //an absent param must not become a 0 s debounce
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime(""));
}

TEST(GpioDebounceTimeTest, GarbageFallsBack)
{
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("abc"));
    //from_string() requires eof: trailing garbage is not a valid number
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("0.2xx"));
}

TEST(GpioDebounceTimeTest, ZeroAndNegativeFallBack)
{
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("0"));
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("-1"));
}

TEST(GpioDebounceTimeTest, AberrantValueFallsBack)
{
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("5.1"));
    EXPECT_DOUBLE_EQ(0.05, GpioCtrl::parseDebounceTime("1e10"));
}

TEST(GpioDebounceTimeTest, CustomFallbackIsHonored)
{
    EXPECT_DOUBLE_EQ(0.1, GpioCtrl::parseDebounceTime("", 0.1));
}

TEST(GpioDebounceTimeTest, ValidityPredicateBounds)
{
    EXPECT_TRUE(GpioCtrl::debounceTimeValid(0.05));
    EXPECT_TRUE(GpioCtrl::debounceTimeValid(5.0));
    EXPECT_FALSE(GpioCtrl::debounceTimeValid(0.0));
    EXPECT_FALSE(GpioCtrl::debounceTimeValid(-0.5));
    EXPECT_FALSE(GpioCtrl::debounceTimeValid(5.0001));
    EXPECT_FALSE(GpioCtrl::debounceTimeValid(std::nan("")));
}

} //namespace
