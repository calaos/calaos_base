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
#include <type_traits>
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

/*----------------------------------------------------------------------------
 * ⭐ THE (request, result) PAIR OF THE REPLY SLOT, ASKED OF THE TYPE SYSTEM.
 *
 * Two std::string in a row, handed over positionally by the single emission
 * site: a permutation there is the SAME TEXT. Nothing - no compiler, no flag,
 * no behavioural case - can ever report it, and every callback parses the
 * second one, so the whole chain would read the command it sent instead of the
 * answer it got. A type identity is the only thing that can hold this.
 *
 * ⚠️ Every EXPECT_FALSE below would pass for free against a slot that accepts
 * nothing, so each is paired with a control that must stay TRUE. The two
 * argument types are read OUT of the shipped typedef, never named: a rename
 * breaks the build instead of quietly disarming the case.
 *--------------------------------------------------------------------------*/

namespace
{

template<typename S> struct ReplySlotArgs;

template<template<typename, typename...> class S, typename A, typename B>
struct ReplySlotArgs<S<void, bool, A, B, Calaos::AudioPlayerData>>
{
    using Req = A;
    using Res = B;
};

using ReqArg = ReplySlotArgs<Calaos::SqueezeRequest_cb>::Req;
using ResArg = ReplySlotArgs<Calaos::SqueezeRequest_cb>::Res;
using SigReqArg = ReplySlotArgs<Calaos::SqueezeRequest_signal>::Req;
using SigResArg = ReplySlotArgs<Calaos::SqueezeRequest_signal>::Res;

//The yardstick, and a LOCAL type so it answers the same on both sides of the
//typing: a slot spelled with two bare strings really does take them either way
//round, in total silence.
using ABarePair = sigc::slot<void, bool, string, string,
                             Calaos::AudioPlayerData>;

//Positive control for the `explicit` lines.
struct AnImplicitWrapper
{
    string v;
    AnImplicitWrapper(const string &s): v(s) {}
};

//Positive control for the conversion-back lines: an is_convertible_v that
//answered FALSE to everything would pass them for free.
struct ALeakyWrapper
{
    string v;
    explicit ALeakyWrapper(const string &s): v(s) {}
    operator string() const { return v; }
};

} //anonymous namespace

TEST(SqueezeboxReplyPair, TheReplySlotCarriesTwoRolesNotTwoStrings)
{
    EXPECT_TRUE((std::is_invocable_v<ABarePair, bool, string, string,
                                     Calaos::AudioPlayerData>))
        << "the yardstick is broken: a bare (string, string) slot no longer "
           "takes two strings, so nothing below measures anything";
    EXPECT_TRUE((std::is_same_v<ReplySlotArgs<ABarePair>::Req,
                                ReplySlotArgs<ABarePair>::Res>))
        << "the two members of a bare pair must be the SAME type - that is "
           "the whole reason no compiler can ever diagnose a permutation";

    EXPECT_FALSE((std::is_same_v<ReqArg, ResArg>))
        << "the reply slot still carries two parameters of the SAME type in a "
           "row: the command sent and the answer received are interchangeable";
    EXPECT_FALSE((std::is_same_v<SigReqArg, SigResArg>))
        << "the reply signal still carries two parameters of the SAME type in "
           "a row";
}

TEST(SqueezeboxReplyPair, TheReplySlotRefusesAPermutedRequestAndResult)
{
    EXPECT_TRUE((std::is_invocable_v<Calaos::SqueezeRequest_cb, bool, ReqArg,
                                     ResArg, Calaos::AudioPlayerData>))
        << "the correctly ordered call is refused - every EXPECT_FALSE in "
           "this case is passing for free";

    EXPECT_FALSE((std::is_invocable_v<Calaos::SqueezeRequest_cb, bool, ResArg,
                                      ReqArg, Calaos::AudioPlayerData>))
        << "the command and the answer are still interchangeable at every "
           "callback that carries them";
    EXPECT_FALSE((std::is_invocable_v<Calaos::SqueezeRequest_cb, bool, string,
                                      string, Calaos::AudioPlayerData>))
        << "a bare pair of strings still reaches the reply slot, so any "
           "callback written next inherits no net";

    EXPECT_TRUE((std::is_invocable_v<Calaos::SqueezeRequest_signal, bool,
                                     SigReqArg, SigResArg,
                                     Calaos::AudioPlayerData>))
        << "the correctly ordered emission is refused";
    EXPECT_FALSE((std::is_invocable_v<Calaos::SqueezeRequest_signal, bool,
                                      SigResArg, SigReqArg,
                                      Calaos::AudioPlayerData>))
        << "the one emission site can still hand the answer over as the "
           "command";
}

TEST(SqueezeboxReplyPair, TheRoleWrapperShapeIsWhatCloses)
{
    EXPECT_TRUE((std::is_convertible_v<string, AnImplicitWrapper>))
        << "the control for the explicit lines below no longer converts";
    EXPECT_TRUE((std::is_convertible_v<ALeakyWrapper, string>))
        << "the control for the leak lines below no longer leaks";

    EXPECT_TRUE((std::is_constructible_v<ReqArg, string>));
    EXPECT_FALSE((std::is_convertible_v<string, ReqArg>))
        << "a bare string is still a request";
    EXPECT_TRUE((std::is_constructible_v<ResArg, string>));
    EXPECT_FALSE((std::is_convertible_v<string, ResArg>))
        << "a bare string is still a result";

    EXPECT_FALSE((std::is_convertible_v<ReqArg, ResArg>))
        << "a request still converts into a result";
    EXPECT_FALSE((std::is_convertible_v<ResArg, ReqArg>))
        << "a result still converts into a request";

    EXPECT_FALSE((std::is_convertible_v<ReqArg, string>))
        << "a request still decays to a bare string";
    EXPECT_FALSE((std::is_convertible_v<ResArg, string>))
        << "a result still decays to a bare string";
}
