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
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
//Level of the lines the calaos_mcp sidecar prints: a healthy box must not log
//[ERR] just because Python logging writes INFO to stderr.
//
#include "McpLogLevel.h"
#include <gtest/gtest.h>

using namespace Calaos::McpLogLevel;

TEST(McpLogLevel, EachPythonLevelMapsToItsJournalLevel)
{
    Classifier c(Level::Warning);
    EXPECT_EQ(Level::Debug, c.classify("DEBUG calaos_mcp.client: ping"));
    EXPECT_EQ(Level::Info, c.classify("INFO calaos_mcp.server: listening"));
    EXPECT_EQ(Level::Warning, c.classify("WARNING calaos_mcp.auth: slow"));
    EXPECT_EQ(Level::Error, c.classify("ERROR calaos_mcp.client: lost"));
    EXPECT_EQ(Level::Error, c.classify("CRITICAL calaos_mcp: fatal"));
}

TEST(McpLogLevel, UvicornColonFormIsUnderstood)
{
    Classifier c(Level::Warning);
    EXPECT_EQ(Level::Info, c.classify("INFO:     Started server process [1]"));
}

TEST(McpLogLevel, LevelNameMustBeAWholeWord)
{
    Level l;
    EXPECT_FALSE(parse("INFORMATION about things", l));
    EXPECT_FALSE(parse("ERRORS everywhere", l));
    EXPECT_FALSE(parse("INFO", l));
    EXPECT_FALSE(parse("", l));
    EXPECT_FALSE(parse("  INFO x", l));
}

TEST(McpLogLevel, UnknownLineOnStderrIsNeverBelowWarning)
{
    Classifier c(Level::Warning);
    EXPECT_EQ(Level::Warning, c.classify("something printed bare"));
    c.classify("INFO calaos_mcp.server: ok");
    EXPECT_EQ(Level::Warning, c.classify("  File \"x.py\", line 3, in f"));
}

TEST(McpLogLevel, TracebackKeepsTheLevelOfItsErrorLine)
{
    Classifier c(Level::Warning);
    EXPECT_EQ(Level::Error, c.classify("ERROR calaos_mcp.server: failed"));
    EXPECT_EQ(Level::Error, c.classify("Traceback (most recent call last):"));
    EXPECT_EQ(Level::Error, c.classify("  File \"x.py\", line 3, in f"));
    EXPECT_EQ(Level::Error, c.classify("ValueError: boom"));
    EXPECT_EQ(Level::Info, c.classify("INFO calaos_mcp.server: recovered"));
}

TEST(McpLogLevel, CrashTracebackAfterInfoIsAnError)
{
    Classifier c(Level::Warning);
    c.classify("INFO calaos_mcp.server: ok");
    EXPECT_EQ(Level::Error, c.classify("Traceback (most recent call last):"));
    EXPECT_EQ(Level::Error, c.classify("RuntimeError: dead"));
}

TEST(McpLogLevel, StdoutDefaultsToInfo)
{
    Classifier c(Level::Info);
    EXPECT_EQ(Level::Info, c.classify("plain message"));
}
