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

/*
 * T3.43 / FINDINGS.md F-WAGO-8 - libmbus sizes its copies from the RESPONSE.
 *
 * WHAT THIS SUITE EXECUTES. The real, shipped libmbus objects:
 * IO/Wago/libmbus/mbus_cmd.o and IO/Wago/libmbus/mbus_rqst.o, linked in from
 * the calaos_server build tree. Nothing here is a copy of the library. The
 * ONLY thing replaced is the pair of socket primitives mbus_sock_read() /
 * mbus_sock_write() (mbus_sock.c is deliberately NOT linked), so a test can
 * hand libmbus a response frame that a Wago PLC would never send - which is
 * exactly the frame the defect is about, since MODBUS/TCP is unauthenticated
 * and the frame can come from a broken PLC or from anyone else on the LAN.
 *
 * THE DEFECT. mbus_cmd_read_coil_status() takes its copy length from the
 * response byte-count field:
 *
 *     mbus_ubyte byte_count = MBUS_BYTE_RD(bufptr);
 *     while (byte_count--) MBUS_BYTE_WR(coils_data, *bufptr++);
 *
 * `byte_count` is an mbus_ubyte, so the copy is bounded by 255 and by NOTHING
 * the caller passed. WagoCtrl::read_bits(0, 1, ...) - the modbus heartbeat,
 * every ten seconds - allocates coilBufferSize(1) == ONE byte, and every Wago
 * input polls with nb == 1 too. mbus_cmd_read_holding_registers() has the same
 * shape through `data_count = MBUS_BYTE_RD(bufptr) / 2`, up to 127 words into
 * a one-word allocation. And mbus_rqst() reads the response body with a length
 * taken from the frame - up to 255 bytes into the 254 bytes that remain in
 * mbus_struct::buf after its header, so mbus_struct itself is overrun by one
 * byte before any of the above even runs.
 *
 * THE ORACLE PROVES THE OVERFLOW, IT DOES NOT ASSUME IT. Every buffer handed
 * to libmbus here is a slice of a much larger arena pre-filled with a
 * sentinel, so the writes past the caller's size land in memory the test owns
 * and can COUNT. That matters twice over: a heap overflow does not reliably
 * crash, so "it did not segfault" would be a green for the wrong reason; and
 * the arena reports the index of the first byte written past the allowance,
 * with -1 - NOT 0 - meaning "stayed inside", so no oracle in this file expects
 * the value zero.
 *
 * FIXTURE. The announced byte-counts are deliberately far from, and asymmetric
 * to, what the request asked for (1 coil vs 200 bytes announced; 17 coils vs 1
 * byte announced; 3 registers vs 250 bytes announced): a byte-count that
 * happens to equal the requested quantity is indistinguishable from the
 * correct case and would pin nothing. Case data is asymmetric and
 * non-palindromic for the same reason.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <vector>

#include "mbus.h"
#include "WagoBits.h"

namespace
{

/* ------------------------------------------------------------------------ *
 * The forged wire. mbus_sock_read() serves these bytes in order.
 * ------------------------------------------------------------------------ */

std::vector<unsigned char> g_wire;
size_t g_wire_pos = 0;

/* The length field and the body are armed SEPARATELY on purpose: the whole
 * defect is that libmbus believes the announced sizes rather than counting
 * what it actually holds. */
void armResponse(unsigned char announcedLength,
                 const std::vector<unsigned char> &body)
{
    g_wire.clear();
    g_wire.push_back(0x00);            /* transaction id high */
    g_wire.push_back(0x00);            /* transaction id low  */
    g_wire.push_back(0x00);            /* protocol id high    */
    g_wire.push_back(0x00);            /* protocol id low     */
    g_wire.push_back(0x00);            /* length high         */
    g_wire.push_back(announcedLength); /* length low          */
    g_wire.insert(g_wire.end(), body.begin(), body.end());
    g_wire_pos = 0;
}

std::vector<unsigned char> coilBody(unsigned char unitId,
                                    unsigned char functionCode,
                                    unsigned char announcedByteCount,
                                    const std::vector<unsigned char> &data)
{
    std::vector<unsigned char> body;
    body.push_back(unitId);
    body.push_back(functionCode);
    body.push_back(announcedByteCount);
    body.insert(body.end(), data.begin(), data.end());
    return body;
}

std::vector<unsigned char> filler(size_t n, unsigned char v)
{
    return std::vector<unsigned char>(n, v);
}

/* ------------------------------------------------------------------------ *
 * The arenas. A caller buffer is a SLICE of one of these; everything past the
 * slice is sentinel and belongs to the test, so an overflow is observable
 * instead of merely fatal-or-not.
 * ------------------------------------------------------------------------ */

const unsigned char BYTE_SENTINEL = 0xa5;
const unsigned short WORD_SENTINEL = 0xa5a5;

/* Fill of everything around mbus_struct. Distinct from BYTE_SENTINEL so that
 * a byte libmbus read from PAST the end of mbus_struct::buf can be told apart
 * from a byte it simply never wrote. Namespace scope, not a class member: an
 * in-class static const is not defined anywhere and EXPECT_EQ() binds its
 * arguments by reference. */
const unsigned char MBUS_GUARD = 0xc3;

class ByteArena
{
public:
    ByteArena(): bytes(1024, BYTE_SENTINEL) {}

    mbus_ubyte *base() { return &bytes[0]; }

    /* Index of the first byte at or after `allowed` that libmbus wrote, or -1
     * when the copy stayed inside the caller's buffer. -1 and not 0: 0 is a
     * legal index here, and an oracle whose pass value is 0 is the one shape
     * that cannot be told apart from memory that merely happens to be zero. */
    int firstByteWrittenPast(size_t allowed) const
    {
        for (size_t i = allowed; i < bytes.size(); i++)
            if (bytes[i] != BYTE_SENTINEL)
                return (int)i;
        return -1;
    }

    /* Index of the first byte equal to `v`, or -1. Used to catch bytes that
     * libmbus read from PAST the end of mbus_struct::buf and copied out. */
    int firstByteEqualTo(unsigned char v) const
    {
        for (size_t i = 0; i < bytes.size(); i++)
            if (bytes[i] == v)
                return (int)i;
        return -1;
    }

    unsigned char at(size_t i) const { return bytes[i]; }

private:
    std::vector<unsigned char> bytes;
};

class WordArena
{
public:
    WordArena(): words(512, WORD_SENTINEL) {}

    mbus_uword *base() { return &words[0]; }

    int firstWordWrittenPast(size_t allowed) const
    {
        for (size_t i = allowed; i < words.size(); i++)
            if (words[i] != WORD_SENTINEL)
                return (int)i;
        return -1;
    }

    unsigned short at(size_t i) const { return words[i]; }

private:
    std::vector<unsigned short> words;
};

/* ------------------------------------------------------------------------ *
 * A guarded mbus_struct. mbus_struct::buf is its LAST member, so an overrun
 * of buf leaves the struct; the extra bytes past sizeof(mbus_struct) are
 * filled with GUARD, and so are the trailing pad bytes of the struct itself,
 * which is where the one-byte overrun of mbus_rqst() actually lands.
 * ------------------------------------------------------------------------ */

class Plc
{
public:
    Plc(): raw(sizeof(mbus_struct) + 64, MBUS_GUARD)
    {
        mbus_struct *m = handle();
        m->sd = 3;             /* the socket layer is stubbed; never used */
        m->timeout = 1;
        m->flags = 0;
        m->ex_code = 0;
        m->is_initialized = MBUS_FL_IS_INITIALIZED;
    }

    mbus_struct *handle() { return reinterpret_cast<mbus_struct *>(&raw[0]); }

    /* Byte number `n` PAST the declared end of mbus_struct::buf. */
    unsigned char byteAfterBuf(size_t n) const
    {
        return raw[offsetof(mbus_struct, buf) + MBUS_HDR_LEN + MBUS_DATA_LEN + n];
    }

private:
    std::vector<unsigned char> raw;
};

} //namespace

/* ------------------------------------------------------------------------ *
 * The stubbed socket. mbus_sock.c is not linked; these two are what
 * mbus_rqst.o calls. C linkage, matching mbus_sock.c exactly.
 * ------------------------------------------------------------------------ */

extern "C" int mbus_sock_read(int, mbus_ubyte *buf, int len, int)
{
    if (len < 0)
        return -1;
    if (g_wire_pos + (size_t)len > g_wire.size())
        return -1; /* the peer did not send that much: short read */
    if (len > 0)
        memcpy(buf, &g_wire[g_wire_pos], (size_t)len);
    g_wire_pos += (size_t)len;
    return len;
}

extern "C" int mbus_sock_write(int, mbus_ubyte *, int len, int)
{
    return len;
}

/* ======================================================================== *
 * FC 01 - read coil status
 * ======================================================================== */

/* THE HEARTBEAT. WagoMap::WagoModbusHeartBeatTick() calls read_bits(0, 1, ...)
 * every ten seconds, and every Wago digital input polls with nb == 1 as well,
 * so the caller's buffer is coilBufferSize(1) == ONE byte. A response
 * announcing 200 bytes writes 199 of them past it. */
TEST(MbusResponseTest, AHeartbeatSizedCoilBufferIsNotWrittenPastItsOneByte)
{
    Plc plc;
    ByteArena arena;
    const size_t allowed = (size_t)WagoBits::coilBufferSize(1);
    ASSERT_EQ(1u, allowed);

    armResponse(203, coilBody(1, 1, 200, filler(200, 0x3c)));

    mbus_cmd_read_coil_status(plc.handle(), 1, 0, 1, arena.base());

    EXPECT_EQ(-1, arena.firstByteWrittenPast(allowed));
}

/* A byte-count that contradicts the request is an INVALID response, not a
 * response to be clamped: refuse it. */
TEST(MbusResponseTest, ACoilByteCountLargerThanTheRequestIsRefused)
{
    Plc plc;
    ByteArena arena;

    armResponse(203, coilBody(1, 1, 200, filler(200, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 1, arena.base()));
}

/* The other direction, and the reason truncation is not an option either: 17
 * coils need three bytes, the response announces one. Accepting it would let
 * WagoCtrl::read_bits() read bits 8..16 out of two bytes nobody wrote and
 * publish them as PLC input states. */
TEST(MbusResponseTest, ACoilByteCountSmallerThanTheRequestIsRefusedNotPartiallyFilled)
{
    Plc plc;
    ByteArena arena;

    armResponse(4, coilBody(1, 1, 1, filler(1, 0xe7)));

    EXPECT_EQ(-1, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 17, arena.base()));
    EXPECT_EQ(-1, arena.firstByteWrittenPast(0));
}

/* The acquis: a response that agrees with the request is still delivered,
 * byte for byte, and still writes nothing past the buffer. Green before the
 * fix and green after - this is what tells a real red from a uniform one. */
TEST(MbusResponseTest, ACoilResponseConsistentWithTheRequestIsStillDelivered)
{
    Plc plc;
    ByteArena arena;
    std::vector<unsigned char> data;
    data.push_back(0x8d);
    data.push_back(0x1e);
    data.push_back(0x01);

    armResponse(6, coilBody(1, 1, 3, data));

    EXPECT_EQ(0, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 17, arena.base()));
    EXPECT_EQ(0x8d, arena.at(0));
    EXPECT_EQ(0x1e, arena.at(1));
    EXPECT_EQ(0x01, arena.at(2));
    EXPECT_EQ(-1, arena.firstByteWrittenPast(3));
}

/* A byte-count can agree with the request and STILL not be in the frame.
 * 2040 coils legitimately need 255 bytes, but only 251 data bytes fit in a
 * response body, so the copy runs off the end of mbus_struct::buf and hands
 * the caller four bytes of whatever follows the struct. Checking the count
 * against the request alone does not close this; it must also be inside the
 * frame that was received. */
TEST(MbusResponseTest, ACoilByteCountLargerThanTheReceivedFrameIsRefused)
{
    Plc plc;
    ByteArena arena;

    armResponse(254, coilBody(1, 1, 255, filler(251, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 2040, arena.base()));
    EXPECT_EQ(-1, arena.firstByteEqualTo(MBUS_GUARD));
}

/* ======================================================================== *
 * FC 03 - read holding registers
 * ======================================================================== */

/* Every Wago analog input and every temperature input polls read_words(addr,
 * 1, ...), which allocates ONE mbus_uword. Three are asked for here so the
 * overflow is visible without being confused with an off-by-one. */
TEST(MbusResponseTest, ARegisterReadIsNotWrittenPastTheRequestedWordCount)
{
    Plc plc;
    WordArena arena;

    armResponse(253, coilBody(1, 3, 250, filler(250, 0x3c)));

    mbus_cmd_read_holding_registers(plc.handle(), 1, 0, 3, arena.base());

    EXPECT_EQ(-1, arena.firstWordWrittenPast(3));
}

TEST(MbusResponseTest, ARegisterByteCountLargerThanTheRequestIsRefused)
{
    Plc plc;
    WordArena arena;

    armResponse(253, coilBody(1, 3, 250, filler(250, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_read_holding_registers(plc.handle(), 1, 0, 3, arena.base()));
}

/* ⭐ The case that says WHICH quantity has to be checked. The shipped code
 * keeps only `byte_count / 2`, and 7 / 2 == 3 == the three registers asked
 * for, so a check written on the halved value accepts an odd byte-count that
 * no MODBUS slave can produce. The raw announced byte-count is the thing to
 * confront with the request. */
TEST(MbusResponseTest, AnOddRegisterByteCountIsRefusedEvenWhenItsHalfMatches)
{
    Plc plc;
    WordArena arena;

    armResponse(10, coilBody(1, 3, 7, filler(7, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_read_holding_registers(plc.handle(), 1, 0, 3, arena.base()));
}

/* The register acquis. Words are big endian on the wire; the three values are
 * distinct, asymmetric and non-palindromic so a swapped pair would show. */
TEST(MbusResponseTest, ARegisterResponseConsistentWithTheRequestIsStillDelivered)
{
    Plc plc;
    WordArena arena;
    std::vector<unsigned char> data;
    data.push_back(0x12); data.push_back(0x34);
    data.push_back(0xab); data.push_back(0xcd);
    data.push_back(0x00); data.push_back(0x07);

    armResponse(9, coilBody(1, 3, 6, data));

    EXPECT_EQ(0, mbus_cmd_read_holding_registers(plc.handle(), 1, 0, 3, arena.base()));
    EXPECT_EQ(0x1234, arena.at(0));
    EXPECT_EQ(0xabcd, arena.at(1));
    EXPECT_EQ(0x0007, arena.at(2));
    EXPECT_EQ(-1, arena.firstWordWrittenPast(3));
}

/* ======================================================================== *
 * mbus_rqst() - the frame itself, before any command looks at it
 * ======================================================================== */

/* mbus_struct::buf holds MBUS_HDR_LEN + MBUS_DATA_LEN == 260 bytes and the
 * body is read at buf + 6, so 254 bytes fit. The length field is an
 * mbus_ubyte: a peer announcing 255 overruns the struct by one byte. */
TEST(MbusResponseTest, AResponseFrameLongerThanTheBufferIsRefused)
{
    Plc plc;
    ByteArena arena;
    std::vector<unsigned char> data;
    data.push_back(0x8d);
    data.push_back(0x1e);
    data.push_back(0x01);
    std::vector<unsigned char> body = coilBody(1, 1, 3, data);
    std::vector<unsigned char> tail = filler(249, 0x3c);
    body.insert(body.end(), tail.begin(), tail.end());
    ASSERT_EQ(255u, body.size());

    armResponse(255, body);

    EXPECT_EQ(-1, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 17, arena.base()));
    EXPECT_EQ(MBUS_GUARD, plc.byteAfterBuf(0));
}

/* The boundary, from the legal side: 254 bytes is the largest body that fits
 * and it must still go through. Without this case, refusing every frame would
 * look like a fix. */
TEST(MbusResponseTest, AResponseFrameThatExactlyFillsTheBufferIsStillAccepted)
{
    Plc plc;
    ByteArena arena;
    std::vector<unsigned char> data;
    data.push_back(0x8d);
    data.push_back(0x1e);
    data.push_back(0x01);
    std::vector<unsigned char> body = coilBody(1, 1, 3, data);
    std::vector<unsigned char> tail = filler(248, 0x3c);
    body.insert(body.end(), tail.begin(), tail.end());
    ASSERT_EQ(254u, body.size());

    armResponse(254, body);

    EXPECT_EQ(0, mbus_cmd_read_coil_status(plc.handle(), 1, 0, 17, arena.base()));
    EXPECT_EQ(0x8d, arena.at(0));
    EXPECT_EQ(0x1e, arena.at(1));
    EXPECT_EQ(0x01, arena.at(2));
    EXPECT_EQ(MBUS_GUARD, plc.byteAfterBuf(0));
}

/* ======================================================================== *
 * FC 17 - report slave id
 * ======================================================================== */

/* FC 17 carries NO requested quantity, so there is nothing to confront the
 * announced byte-count with except the frame that arrived. It has no caller
 * anywhere in the tree today; what is pinned here is that it stops at the end
 * of what was actually received instead of copying 200 bytes out of a body
 * that holds 17. */
TEST(MbusResponseTest, AReportSlaveIdByteCountLargerThanTheFrameIsRefused)
{
    Plc plc;
    ByteArena arena;
    mbus_ubyte count = 0xff;

    armResponse(20, coilBody(1, 17, 200, filler(17, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_report_slave_id(plc.handle(), 1, &count, arena.base()));
    EXPECT_EQ(-1, arena.firstByteWrittenPast(17));
}

/* The boundary of "inside the frame", from the illegal side, one byte out.
 * The frame carries 17 data bytes and announces 18. Without this case the term
 * that subtracts the unit id, the function code and the byte-count byte itself
 * is only ever exercised far from its edge, and an off-by-one in it would go
 * unnoticed - its green twin below announces exactly the 17 that are there. */
TEST(MbusResponseTest, AReportSlaveIdByteCountOneBeyondTheFrameIsRefused)
{
    Plc plc;
    ByteArena arena;
    mbus_ubyte count = 0xff;

    armResponse(20, coilBody(1, 17, 18, filler(17, 0x3c)));

    EXPECT_EQ(-1, mbus_cmd_report_slave_id(plc.handle(), 1, &count, arena.base()));
    EXPECT_EQ(-1, arena.firstByteWrittenPast(17));
}

TEST(MbusResponseTest, AReportSlaveIdResponseInsideItsFrameIsStillDelivered)
{
    Plc plc;
    ByteArena arena;
    mbus_ubyte count = 0xff;
    std::vector<unsigned char> data = filler(17, 0x3c);
    data[0] = 0x57; /* asymmetric ends so a reversed copy would show */
    data[16] = 0x0b;

    armResponse(20, coilBody(1, 17, 17, data));

    EXPECT_EQ(0, mbus_cmd_report_slave_id(plc.handle(), 1, &count, arena.base()));
    EXPECT_EQ(17, count);
    EXPECT_EQ(0x57, arena.at(0));
    EXPECT_EQ(0x0b, arena.at(16));
    EXPECT_EQ(-1, arena.firstByteWrittenPast(17));
}
