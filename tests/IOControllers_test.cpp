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

// T1.19 — IO controllers (MySensors/Gpio/Web) regression tests.
//
// - WebCtrl::getValueText: an empty or delimiter-only path used to index
//   tokens[0] on an empty vector (Utils::split with max=0 does not pad).
// - MySensorsController::sendMessage: used to dereference a null
//   serial/TCP handle when the transport is down (before it is opened or
//   while reconnecting).
//
// The GpioCtrl destructor fd-leak fix (inverted close() guard) is not unit
// tested here: the fd is only ever opened through the hardcoded
// /sys/class/gpio/... paths, which do not exist off-target.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "WebCtrl.h"
#include "MySensorsController.h"

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

TEST(MySensorsControllerTest, SendOnSerialGatewayWithoutTransportDoesNotCrash)
{
    Params p;
    p.Add("gateway", "serial");
    p.Add("port", "/nonexistent/calaos_t119_tty");

    //The serial port cannot be opened: serialHandle stays null and a
    //retry timer is armed. Sending a message in that state used to
    //dereference the null handle and crash.
    MySensorsController ctrl(p);
    ctrl.setValue("12", "1", 2, "42");

    //Cache is still functional and empty
    EXPECT_EQ("", ctrl.getValue("12", "1"));
}

TEST(MySensorsControllerTest, SendOnTcpGatewayWhileDisconnectedDoesNotCrash)
{
    Params p;
    p.Add("gateway", "tcp");
    p.Add("host", "invalid host name");
    p.Add("port", "5003");

    //The connect fails (invalid address): the handle is closing/closed
    //while waiting for the reconnect timer. Sending must not crash.
    MySensorsController ctrl(p);
    ctrl.setValue("12", "1", 2, "42");

    EXPECT_EQ("", ctrl.getValue("12", "1"));
}

} //namespace
