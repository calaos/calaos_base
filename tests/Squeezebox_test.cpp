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

// T3.5 — Squeezebox protocol helper regressions.
//
// - Squeezebox::reassembleMessages: the buffered-fragment path used to
//   overwrite msg with the buffer, discarding the just-arrived chunk that
//   carried the terminating newline; and the trailing-trim loop evaluated
//   msg[i] before the i >= 0 guard, reading msg[-1] on an all-newline chunk
//   (aborts under a hardened libstdc++, plain UB otherwise).
// - Squeezebox::coverArtUrl: aid was defaulted to "current" before being
//   tested for emptiness, so the ?playerid= fallback was unreachable.
//
// The helpers are static and header-inline in Squeezebox.h, so nothing of
// the server binary is linked here.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Squeezebox.h"

using Calaos::Squeezebox;
using std::string;
using std::vector;

TEST(SqueezeboxReassembly, IncompleteChunkIsBuffered)
{
    string buffer;
    vector<string> tokens;

    EXPECT_FALSE(Squeezebox::reassembleMessages(buffer, "ab cd", tokens));
    EXPECT_EQ("ab cd", buffer);
    EXPECT_TRUE(tokens.empty());

    //a second fragment accumulates
    EXPECT_FALSE(Squeezebox::reassembleMessages(buffer, " ef", tokens));
    EXPECT_EQ("ab cd ef", buffer);
    EXPECT_TRUE(tokens.empty());
}

TEST(SqueezeboxReassembly, CompletingChunkIsNotDiscarded)
{
    string buffer;
    vector<string> tokens;

    ASSERT_FALSE(Squeezebox::reassembleMessages(buffer, "player id 00%3A04 art", tokens));
    ASSERT_TRUE(Squeezebox::reassembleMessages(buffer, "ist ?\n", tokens));

    //the old code produced "player id 00%3A04 art": the chunk carrying
    //the terminating newline was dropped
    ASSERT_EQ(1u, tokens.size());
    EXPECT_EQ("player id 00%3A04 artist ?", tokens[0]);
    EXPECT_TRUE(buffer.empty());
}

TEST(SqueezeboxReassembly, FragmentPlusMultilineChunk)
{
    string buffer;
    vector<string> tokens;

    ASSERT_FALSE(Squeezebox::reassembleMessages(buffer, "one", tokens));
    ASSERT_TRUE(Squeezebox::reassembleMessages(buffer, " a\r\ntwo b\r\nthree c\n", tokens));

    ASSERT_EQ(3u, tokens.size());
    EXPECT_EQ("one a", tokens[0]);
    EXPECT_EQ("two b", tokens[1]);
    EXPECT_EQ("three c", tokens[2]);
    EXPECT_TRUE(buffer.empty());
}

TEST(SqueezeboxReassembly, UnbufferedMultilineChunk)
{
    string buffer;
    vector<string> tokens;

    ASSERT_TRUE(Squeezebox::reassembleMessages(buffer, "one\ntwo\r\n", tokens));

    ASSERT_EQ(2u, tokens.size());
    EXPECT_EQ("one", tokens[0]);
    EXPECT_EQ("two", tokens[1]);
}

TEST(SqueezeboxReassembly, AllNewlineChunkIsSafe)
{
    string buffer;
    vector<string> tokens;

    //the old trim loop read msg[-1] here (OOB read)
    EXPECT_TRUE(Squeezebox::reassembleMessages(buffer, "\r\n\r\n", tokens));
    EXPECT_TRUE(tokens.empty());
    EXPECT_TRUE(buffer.empty());
}

TEST(SqueezeboxCoverUrl, TrackArtworkId)
{
    EXPECT_EQ("http://lms.local:9000/music/1234/cover.jpg",
              Squeezebox::coverArtUrl("lms.local", 9000, "1234", "00:11:22:33:44:55"));
}

TEST(SqueezeboxCoverUrl, EmptyIdFallsBackToCurrentWithPlayerid)
{
    //the ?playerid= branch was unreachable before T3.5 (aid was defaulted
    //to "current" before being tested for emptiness)
    EXPECT_EQ("http://lms.local:9002/music/current/cover.jpg?playerid=00:11:22:33:44:55",
              Squeezebox::coverArtUrl("lms.local", 9002, "", "00:11:22:33:44:55"));
}
