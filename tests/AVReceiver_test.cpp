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

// T3.1 — AVReceiver correctness regression tests.
//
// - AVROnkyo eISCP reassembly: brecv_buffer.insert() used data.end() (an
//   iterator into another vector) as insertion position -> UB. Both the
//   "prepend buffered chunk" and the "buffer trailing partial" paths are
//   exercised through split packets.
// - AVROnkyo SLZ/SL3: zone-2/3 input-source updates emitted source_main
//   instead of the zone's own value.
// - AVROnkyo hex volume: is_of_type<int> (decimal) dropped hex levels
//   containing letters (MVL1A = 26).
// - AVRPioneer YV (zone-3 volume) fired state_changed_2.
// - AVRDenon/AVRMarantz/AVROnkyo setVolume: width()/fill() were applied to
//   the literal command prefix instead of the numeric value -> commands not
//   zero-padded (MV5 instead of MV05).
// - AVRManager: Create() returns NULL for an unknown model and IOAVReceiver's
//   dtor calls Delete() unconditionally -> Delete(NULL) must be a no-op.
//
// The receivers' constructors only queue a TCP connect on the default uvw
// loop; the loop is never run here, so no network I/O ever happens and no
// callback fires. sendRequest() is virtual and overridden to capture the
// outgoing protocol instead of writing to the socket.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "AVRDenon.h"
#include "AVRManager.h"
#include "AVRMarantz.h"
#include "AVROnkyo.h"
#include "AVRPioneer.h"
#include "AVReceiver.h"

using namespace Calaos;

namespace
{

//Records (param, value) pairs emitted on a state_changed_N signal
struct SigRecorder
{
    std::vector<std::pair<std::string, std::string>> events;

    void on(std::string p, std::string v) { events.emplace_back(p, v); }

    bool has(const std::string &p, const std::string &v) const
    {
        for (const auto &e: events)
            if (e.first == p && e.second == v) return true;
        return false;
    }
};

Params makeParams(const std::string &model)
{
    Params p;
    p.Add("id", "avr_test");
    p.Add("host", "127.0.0.1");
    p.Add("model", model);
    return p;
}

class TestOnkyo: public AVROnkyo
{
public:
    TestOnkyo(Params &p): AVROnkyo(p) {}
    using AVROnkyo::processMessage;

    std::vector<std::vector<char>> frames;
    void sendRequest(string) override {}
    void sendRequest(vector<char> r) override { frames.push_back(std::move(r)); }
};

class TestPioneer: public AVRPioneer
{
public:
    TestPioneer(Params &p): AVRPioneer(p) {}
    using AVRPioneer::processMessage;

    std::vector<std::string> commands;
    void sendRequest(string r) override { commands.push_back(std::move(r)); }
};

class TestDenon: public AVRDenon
{
public:
    TestDenon(Params &p): AVRDenon(p) {}

    std::vector<std::string> commands;
    void sendRequest(string r) override { commands.push_back(std::move(r)); }
};

class TestMarantz: public AVRMarantz
{
public:
    TestMarantz(Params &p): AVRMarantz(p) {}

    std::vector<std::string> commands;
    void sendRequest(string r) override { commands.push_back(std::move(r)); }
};

//Build an eISCP frame the way the receiver sends them:
//16-byte header + "!1" + command + 0x0D
std::vector<char> eiscpFrame(const std::string &cmd)
{
    std::vector<char> f = {'I', 'S', 'C', 'P',
                           0x00, 0x00, 0x00, 0x10,
                           0x00, 0x00, 0x00, (char)(cmd.size() + 3),
                           0x01,
                           0x00, 0x00, 0x00,
                           '!', '1'};
    f.insert(f.end(), cmd.begin(), cmd.end());
    f.push_back(0x0D);
    return f;
}

//Extract the command of a captured outgoing eISCP frame
std::string frameCommand(const std::vector<char> &f)
{
    std::string s;
    for (size_t i = 18; i < f.size() && f[i] != 0x0D; i++)
        s.push_back(f[i]);
    return s;
}

TEST(AVROnkyoTest, ReassemblesPacketSplitInTwoChunks)
{
    Params p = makeParams("onkyo");
    SigRecorder rec;
    TestOnkyo avr(p);
    avr.state_changed_1.connect(sigc::mem_fun(rec, &SigRecorder::on));

    std::vector<char> frame = eiscpFrame("MVL23"); //24 bytes
    ASSERT_EQ(24u, frame.size());

    //first chunk < 18 bytes: gets buffered
    std::vector<char> chunk1(frame.begin(), frame.begin() + 5);
    //second chunk >= 18 bytes: triggers the buffered-prepend path
    std::vector<char> chunk2(frame.begin() + 5, frame.end());
    ASSERT_GE(chunk2.size(), 18u);

    avr.processMessage(chunk1);
    EXPECT_TRUE(rec.events.empty());
    avr.processMessage(chunk2);

    EXPECT_EQ(0x23, avr.getVolume(1));
    EXPECT_TRUE(rec.has("volume", Utils::to_string(0x23)));
}

TEST(AVROnkyoTest, BuffersTrailingPartialFrame)
{
    Params p = makeParams("onkyo");
    SigRecorder rec;
    TestOnkyo avr(p);
    avr.state_changed_1.connect(sigc::mem_fun(rec, &SigRecorder::on));

    std::vector<char> f1 = eiscpFrame("PWR01");
    std::vector<char> f2 = eiscpFrame("MVL10");

    //frame1 + the first 4 bytes of frame2 in one chunk: frame1 is processed,
    //the partial frame2 goes through the trailing-buffer path
    std::vector<char> chunk1 = f1;
    chunk1.insert(chunk1.end(), f2.begin(), f2.begin() + 4);
    avr.processMessage(chunk1);

    EXPECT_TRUE(avr.getPower(1));
    EXPECT_TRUE(rec.has("power", "true"));
    EXPECT_EQ(0, avr.getVolume(1));

    //rest of frame2 completes the message
    std::vector<char> chunk2(f2.begin() + 4, f2.end());
    ASSERT_GE(chunk2.size(), 18u);
    avr.processMessage(chunk2);

    EXPECT_EQ(0x10, avr.getVolume(1));
    EXPECT_TRUE(rec.has("volume", Utils::to_string(0x10)));
}

TEST(AVROnkyoTest, ParsesTwoMessagesInOnePacket)
{
    Params p = makeParams("onkyo");
    SigRecorder rec;
    TestOnkyo avr(p);
    avr.state_changed_1.connect(sigc::mem_fun(rec, &SigRecorder::on));

    std::vector<char> chunk = eiscpFrame("PWR01");
    std::vector<char> f2 = eiscpFrame("MVL23");
    chunk.insert(chunk.end(), f2.begin(), f2.end());

    avr.processMessage(chunk);

    EXPECT_TRUE(avr.getPower(1));
    EXPECT_EQ(0x23, avr.getVolume(1));
}

TEST(AVROnkyoTest, HexVolumeWithLettersIsParsed)
{
    Params p = makeParams("onkyo");
    SigRecorder rec;
    TestOnkyo avr(p);
    avr.state_changed_1.connect(sigc::mem_fun(rec, &SigRecorder::on));

    avr.processMessage(eiscpFrame("MVL1A"));

    EXPECT_EQ(0x1A, avr.getVolume(1));
    EXPECT_TRUE(rec.has("volume", Utils::to_string(0x1A)));
}

TEST(AVROnkyoTest, ZoneSourceChangeEmitsOwnZoneValue)
{
    Params p = makeParams("onkyo");
    SigRecorder rec2, rec3;
    TestOnkyo avr(p);
    avr.state_changed_2.connect(sigc::mem_fun(rec2, &SigRecorder::on));
    avr.state_changed_3.connect(sigc::mem_fun(rec3, &SigRecorder::on));

    //source_main stays AVR_UNKNOWN (0): the old code emitted it instead of
    //the zone's own value
    avr.processMessage(eiscpFrame("SLZ23")); //zone2 -> CD
    avr.processMessage(eiscpFrame("SL326")); //zone3 -> Tuner

    EXPECT_EQ((int)AVReceiver::AVR_INPUT_CD, avr.getInputSource(2));
    EXPECT_TRUE(rec2.has("input_source",
                         Utils::to_string((int)AVReceiver::AVR_INPUT_CD)));
    EXPECT_FALSE(rec2.has("input_source",
                          Utils::to_string((int)AVReceiver::AVR_UNKNOWN)));

    EXPECT_EQ((int)AVReceiver::AVR_INPUT_TUNER, avr.getInputSource(3));
    EXPECT_TRUE(rec3.has("input_source",
                         Utils::to_string((int)AVReceiver::AVR_INPUT_TUNER)));
}

TEST(AVROnkyoTest, SetVolumeIsZeroPaddedHex)
{
    Params p = makeParams("onkyo");
    TestOnkyo avr(p);

    avr.setVolume(5, 1);   //0x05
    avr.setVolume(26, 2);  //0x1A
    ASSERT_EQ(2u, avr.frames.size());
    EXPECT_EQ("MVL05", frameCommand(avr.frames[0]));
    EXPECT_EQ("ZVL1A", frameCommand(avr.frames[1]));
}

TEST(AVRPioneerTest, Zone3VolumeEmitsOnZone3Signal)
{
    Params p = makeParams("pioneer");
    SigRecorder rec2, rec3;
    TestPioneer avr(p);
    avr.state_changed_2.connect(sigc::mem_fun(rec2, &SigRecorder::on));
    avr.state_changed_3.connect(sigc::mem_fun(rec3, &SigRecorder::on));

    avr.processMessage(std::string("YV81")); //81 * 100 / 81 = 100

    EXPECT_EQ(100, avr.getVolume(3));
    EXPECT_TRUE(rec3.has("volume", "100"));
    EXPECT_TRUE(rec2.events.empty()); //no spurious zone-2 event
}

TEST(AVRDenonTest, SetVolumeIsZeroPadded)
{
    Params p = makeParams("denon");
    TestDenon avr(p);

    avr.setVolume(95, 1);  //99 - 94 = 5 -> MV05
    avr.setVolume(100, 1); //99 - 99 = 0 -> MV00
    avr.setVolume(95, 2);  //-> Z205
    ASSERT_EQ(3u, avr.commands.size());
    EXPECT_EQ("MV05", avr.commands[0]);
    EXPECT_EQ("MV00", avr.commands[1]);
    EXPECT_EQ("Z205", avr.commands[2]);
}

TEST(AVRMarantzTest, SetVolumeIsZeroPadded)
{
    Params p = makeParams("marantz");
    TestMarantz avr(p);

    avr.setVolume(95, 1);
    avr.setVolume(95, 3);
    ASSERT_EQ(2u, avr.commands.size());
    EXPECT_EQ("MV05", avr.commands[0]);
    EXPECT_EQ("Z305", avr.commands[1]);
}

TEST(AVRManagerTest, UnknownModelReturnsNullAndDeleteNullIsSafe)
{
    Params p = makeParams("doesnotexist");

    AVReceiver *r = AVRManager::Instance().Create(p);
    EXPECT_EQ(nullptr, r);

    //IOAVReceiver's dtor path: Delete() with the NULL Create() returned
    AVRManager::Instance().Delete(nullptr); //must not crash
}

TEST(AVRManagerTest, SameHostIsRefCountedAndDoubleDeleteIsSafe)
{
    Params p = makeParams("denon");
    p.Add("host", "127.0.0.2");

    AVReceiver *r1 = AVRManager::Instance().Create(p);
    ASSERT_NE(nullptr, r1);
    AVReceiver *r2 = AVRManager::Instance().Create(p);
    EXPECT_EQ(r1, r2); //same host -> shared instance

    AVRManager::Instance().Delete(r1);
    AVRManager::Instance().Delete(r2); //refcount reaches 0, object freed
}

} //namespace
