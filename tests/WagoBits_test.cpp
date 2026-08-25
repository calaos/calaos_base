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

/*******************************************************************************
 * T3.30 - CHARACTERIZATION of the modbus multiple-write buffer assembly.
 *
 * WHAT IS BROKEN (FINDINGS.md F-WAGO-2, three defects on the same ten lines of
 * IO/Wago/WagoCtrl.cpp - write_multiple_bits() and its twin
 * write_multiple_words()):
 *
 *   (a) `nb` comes from the MESSAGE, `values` from the DECODED ARRAY, and the
 *       two are never confronted. `values[i]` for i in [0, nb) then reads out
 *       of bounds. On an EMPTY vector<bool>/vector<UWord> that is not "writes
 *       nothing": _M_start is null and the process takes SIGSEGV 139.
 *
 *   (b) setBit(unsigned char &mot, int pos, bool val) takes a reference to ONE
 *       BYTE, and the caller passes `*data` - always data[0] - with pos = i up
 *       to nb-1. Only bits 0..7 are ever written; 16 setBit calls on one byte
 *       give 0xff. read_bits(), two functions higher up, gets it right:
 *       getBit(*(data + i / 8), i % 8).
 *
 *   (c) new mbus_ubyte[nb / 8 + nb % 8] followed by memset(data, '\0', nb/8):
 *       the memset is SHORTER than the allocation. nb = 17 allocates 3 bytes
 *       and zeroes 2. The allocation expression is wrong too - (nb + 7) / 8 is
 *       the size of a coil buffer; nb/8 + nb%8 over-allocates (nb = 15 gives 8
 *       bytes for 2 needed), which is why (c) is a DIRTY BYTE and not an
 *       overflow.
 *
 * NOTHING HAS EVER RUN THIS CODE. WagoMap::write_multiple_bits/_words have no
 * caller in the tree (re-measured for this ticket, indirect calls included),
 * so no PLC has ever received this message. This suite exists because the
 * functions LOOK right: the first developer who wires a caller gets all three
 * at once, and two of them are silent - a segfault, or wrong bits on physical
 * relays.
 *
 * THE SEAM is namespace `seam` below. The characterization commit (51962e51)
 * carried the shipped bodies of WagoCtrl.cpp there verbatim, translated to the
 * vector<> signature and NOTHING else; this commit rewires it onto the
 * production header IO/Wago/WagoBits.h with three `using` lines, so a mutation
 * of the SHIPPED packer turns this suite red. Measured, not assumed: swapping
 * `bit / 8` and `bit % 8` inside WagoBits.h reddens four cases, and each of the
 * four mutations of the ticket reddens a DIFFERENT set.
 *
 * ⚠️ WHAT THIS SUITE CANNOT SEE. WagoCtrl.o is linked by no test binary and
 * cannot be: write_multiple_bits() returns on `if (!is_connected())` before
 * reaching its body, and reaching it means a live modbus socket. The CALL
 * SITES in WagoCtrl.cpp are therefore covered by SOURCE TRIPWIRES at the
 * bottom of this file - the JanssonResidues_test pattern - and by nothing
 * else. Permuting `address` and `nb` at those call sites stays green here;
 * closing that needs distinct types on the two parameters and is T3.31's job,
 * not this ticket's.
 *
 * ⚠️ NO ORACLE HERE DESCRIBES UNDEFINED BEHAVIOUR. Every expectation states
 * what a correct packer must produce. Where the shipped seam reaches UB to get
 * there it is said in place (LargeCountsKeepEveryBitInItsOwnByte); the
 * assertion is still on the CORRECT output, never on what the UB happens to
 * do on this machine.
 *
 * ⚠️ A SEGFAULT PRODUCES NO "FAILED" LINE. Against the shipped bodies, the two
 * ...WithANonZeroCountIsRefused cases did not fail, they DIED: SIGSEGV on
 * values[0], exit code -11 - signal 11, the 139 of F-WAGO-2 - and not one red
 * line in the log. The verdict of this suite is therefore the EXIT CODE, one
 * case at a time (--gtest_filter), never the count of red lines. Every run
 * reported by this ticket, characterization and mutations alike, was taken
 * that way.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <WagoBits.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

//SEAM, REWIRED BY THE FIX COMMIT of T3.30. Until this commit this block
//carried the shipped bodies of WagoCtrl.cpp verbatim; it now names the
//production header,
//so every case below executes the code that ships inside calaos_server and
//calaos_wago. Mutate WagoBits.h and this suite goes red - that is the whole
//point of the indirection, and it is checked, not assumed.
namespace seam
{
using WagoBits::coilBufferSize;
using WagoBits::packBits;
using WagoBits::copyValues;
using WagoBits::countIsWritable;
//T3.30/F1 - rewired by the fix commit. Until it, this name was a local
//`{ return true; }` carrying the shipped rule "every count is readable"; it now
//names the production predicate, so a mutation of WagoBits.h reddens the four
//F1 cases below. Measured, not assumed.
using WagoBits::countIsReadable;
} //namespace seam

namespace
{

//THE FIXTURE. Every number below is chosen against a named trap and none of
//them is derived from an answer this file also asserts.
//
//  * FX_NB = 17 is not a multiple of 8. On a multiple of 8 the byte boundary
//    is invisible AND nb%8 == 0 makes the short memset cover the whole
//    allocation, so defects (b) and (c) both vanish from view.
//  * the bit pattern is asymmetric and not a palindrome, and its three bytes
//    are pairwise distinct (0x8d, 0x1e, 0x01): "written somewhere" and
//    "written in the right byte" give different answers.
//  * the LAST byte is NON-ZERO (0x01). A last byte of 0x00 would be satisfied
//    by an uninitialised byte that happened to be zero.
//  * the guard cases straddle values.size() in BOTH directions (17 claimed on
//    5 delivered, 9 claimed on 17 delivered), so it is not tested only against
//    nb == values.size() - the one case it cannot distinguish. That is the
//    F-WAGO-6 trap, where a fixture constant equalled the value computed from
//    it and the oracle was dead.
const int FX_NB = 17;

//bit 0 first. bytes: 0x8d = 1,0,1,1,0,0,0,1 / 0x1e = 0,1,1,1,1,0,0,0 / 0x01
const bool FX_BITS_RAW[FX_NB] = {
    true,  false, true,  true,  false, false, false, true,
    false, true,  true,  true,  true,  false, false, false,
    true
};

vector<bool> seventeenBits()
{
    return vector<bool>(FX_BITS_RAW, FX_BITS_RAW + FX_NB);
}

//The expectation is spelled out as literals, NOT recomputed from FX_BITS_RAW:
//an oracle that repeats the algorithm under test proves nothing.
vector<unsigned char> bytes(int b0, int b1, int b2)
{
    vector<unsigned char> v;
    v.push_back((unsigned char)b0);
    v.push_back((unsigned char)b1);
    v.push_back((unsigned char)b2);
    return v;
}

vector<unsigned char> bytes(int b0, int b1)
{
    vector<unsigned char> v;
    v.push_back((unsigned char)b0);
    v.push_back((unsigned char)b1);
    return v;
}

//The output buffer always arrives DIRTY and OVERSIZED. A correct packer sizes
//it and writes every byte of it; the shipped one leaves the sentinel showing
//exactly where its memset stopped.
const unsigned char FX_SENTINEL = 0xaa;
const size_t FX_SENTINEL_LEN = 8;

vector<unsigned char> dirtyBuffer()
{
    return vector<unsigned char>(FX_SENTINEL_LEN, FX_SENTINEL);
}

string hex(const vector<unsigned char> &v)
{
    static const char *d = "0123456789abcdef";
    string s = "{";
    for (size_t i = 0; i < v.size(); i++)
    {
        if (i) s += ",";
        s += "0x";
        s += d[(v[i] >> 4) & 0x0f];
        s += d[v[i] & 0x0f];
    }
    return s + "}";
}

string readShippedSource(const string &relative)
{
    const string path = string(CALAOS_TOP_SRCDIR) + "/" + relative;
    ifstream f(path.c_str());
    if (!f.is_open()) return string();
    ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} //anonymous namespace

/*******************************************************************************
 * (b) + (c) TOGETHER - the case the ticket is built around
 ******************************************************************************/

//⭐ THE case. 17 bits, three bytes, every one of them determined.
//Shipped: the allocation is 3 (17/8 + 17%8 = 2 + 1), the memset covers 2, and
//every setBit lands in byte 0 - so byte 0 collects the low 8 bits of a 17-fold
//OR, byte 1 comes back zeroed and byte 2 comes back as it was found.
TEST(WagoBits, SeventeenBitsFillThreeBytesEntirely)
{
    vector<unsigned char> out = dirtyBuffer();
    ASSERT_TRUE(seam::packBits(FX_NB, seventeenBits(), out));
    EXPECT_EQ(bytes(0x8d, 0x1e, 0x01), out) << hex(out);
}

//⭐ (b) alone, at the first boundary that exists. Bit 8 is the LOW bit of the
//SECOND byte; the shipped packer sends it to byte 0, where `0x01 << 8` is
//65536 and truncates away to nothing on assignment to an unsigned char.
TEST(WagoBits, BitNineLandsInTheSecondByte)
{
    const bool raw[9] = { true, false, false, true, false, true, false, false,
                          true };
    vector<unsigned char> out = dirtyBuffer();
    ASSERT_TRUE(seam::packBits(9, vector<bool>(raw, raw + 9), out));
    EXPECT_EQ(bytes(0x29, 0x01), out) << hex(out);
}

//(c) alone, size only, no bits involved: (nb + 7) / 8 is the size of a coil
//buffer. nb = 15 is where the shipped expression is worst - 15/8 + 15%8 = 8
//bytes for 2 needed - and read_bits() carries the same expression, so the two
//directions of the same wire must agree on how many bytes nb bits take.
TEST(WagoBits, TheBufferSizeMatchesReadBits)
{
    EXPECT_EQ(1, seam::coilBufferSize(1));
    EXPECT_EQ(1, seam::coilBufferSize(8));
    EXPECT_EQ(2, seam::coilBufferSize(9));
    EXPECT_EQ(2, seam::coilBufferSize(15));
    EXPECT_EQ(2, seam::coilBufferSize(16));
    EXPECT_EQ(3, seam::coilBufferSize(FX_NB));
    EXPECT_EQ(8, seam::coilBufferSize(64));
    EXPECT_EQ(9, seam::coilBufferSize(65));
}

//(b) past the width of an int. `0x01 << pos` with pos >= 32 is UNDEFINED, and
//the shipped packer reaches pos = 39 here.
//⚠️ The expectation below is the CORRECT packing of these 40 bits and nothing
//else; this case does NOT specify what the shift does. Its red in the
//characterization commit is expected, but the mechanism of that red is not
//part of the contract - which is precisely why the fix keeps pos = i % 8, so
//that no shift count above 7 can ever be formed again.
TEST(WagoBits, LargeCountsKeepEveryBitInItsOwnByte)
{
    vector<bool> v(40, false);
    v[0] = true;    //byte 0, bit 0
    v[13] = true;   //byte 1, bit 5
    v[23] = true;   //byte 2, bit 7
    v[32] = true;   //byte 4, bit 0  <- pos 32, the first undefined shift
    v[39] = true;   //byte 4, bit 7

    vector<unsigned char> out = dirtyBuffer();
    ASSERT_TRUE(seam::packBits(40, v, out));

    vector<unsigned char> expected;
    expected.push_back(0x01);
    expected.push_back(0x20);
    expected.push_back(0x80);
    expected.push_back(0x00);
    expected.push_back(0x81);
    EXPECT_EQ(expected, out) << hex(out);
}

/*******************************************************************************
 * (a) - the count and the values are two different things
 ******************************************************************************/

//⭐ The SIGSEGV case, exactly as F-WAGO-2 measured it: an empty vector<bool>
//and a count that says 17. Refusing is the only correct answer - truncating
//would hand the PLC a partial write it cannot tell from a complete one.
//⚠️ In the characterization commit this case does not FAIL, it DIES. Read the
//exit code, not the log.
TEST(WagoBits, AnEmptyValuesVectorWithANonZeroCountIsRefused)
{
    vector<unsigned char> out = dirtyBuffer();
    EXPECT_FALSE(seam::packBits(FX_NB, vector<bool>(), out));
    EXPECT_EQ(dirtyBuffer(), out) << hex(out);
}

//(a) with a NON-empty vector, which is the half that does not crash and would
//therefore go unnoticed: 17 bits claimed, 5 delivered.
TEST(WagoBits, ACountLargerThanTheValuesIsRefused)
{
    const bool raw[5] = { true, true, false, true, false };
    vector<unsigned char> out = dirtyBuffer();
    EXPECT_FALSE(seam::packBits(FX_NB, vector<bool>(raw, raw + 5), out));
    EXPECT_EQ(dirtyBuffer(), out) << hex(out);
}

//The OTHER direction of the mismatch - more values than the count claims - is
//legal and must pack exactly `nb` of them. Without this case the guard could
//be written as `nb != values.size()` and nobody would notice; with it, the
//count is what decides.
TEST(WagoBits, ACountSmallerThanTheValuesPacksOnlyThatManyBits)
{
    vector<unsigned char> out = dirtyBuffer();
    ASSERT_TRUE(seam::packBits(9, seventeenBits(), out));
    EXPECT_EQ(bytes(0x8d, 0x00), out) << hex(out);
}

//A modbus multiple-write carries at least one item. Zero is not a smaller
//write, it is not a write - and it is also the only way the pointer handed to
//mbus_cmd_force_multiple_coils() could be null.
TEST(WagoBits, ANonPositiveCountIsRefused)
{
    vector<unsigned char> out = dirtyBuffer();
    EXPECT_FALSE(seam::packBits(0, seventeenBits(), out));
    EXPECT_FALSE(seam::packBits(-1, seventeenBits(), out));
    EXPECT_FALSE(seam::packBits(-40, seventeenBits(), out));
    EXPECT_EQ(dirtyBuffer(), out) << hex(out);
}

/*******************************************************************************
 * F1 - THE READ DIRECTION, which this ticket left open
 *
 * The ticket hardened the two WRITES by refusing `nb <= 0` (countIsWritable).
 * The two READS were not touched, and they are the half that RUNS: the modbus
 * heartbeat calls read_bits(0, 1, ...) every ten seconds and every Wago poll
 * goes through read_bits()/read_words(). `nb` reaches them from
 * WagoExternProc_main.cpp, out of an `int count;` that is NEVER INITIALISED and
 * whose from_string() return is never read (F-WAGO-7) - so a pipe message with
 * no `count` key hands them arbitrary stack memory, zero and negatives
 * included.
 *
 * WHAT A NON-POSITIVE COUNT DOES, MEASURED AT g++ -std=c++11 FOR THIS TICKET:
 *
 *   coilBufferSize(nb <= 0) is 0, so read_bits() takes `new mbus_ubyte[0]` - a
 *   VALID pointer to ZERO usable bytes, no throw - and then asks the PLC for
 *   (mbus_uword)nb coils: 65535 of them for nb == -1, 0 for nb == 0.
 *   mbus_cmd_read_coil_status() then reads the byte-count field of the RESPONSE
 *   into `mbus_ubyte byte_count` and runs `while (byte_count--) MBUS_BYTE_WR(
 *   coils_data, *bufptr++)`. That loop is bounded by the RESPONSE and by the
 *   width of an unsigned char - up to 255 bytes - and by NOTHING the caller
 *   passed. 255 bytes into a 0-byte allocation is a heap overflow.
 *   read_words() is the same shape through mbus_cmd_read_holding_registers(),
 *   where `mbus_ubyte data_count = MBUS_BYTE_RD(bufptr) / 2` caps at 127 words.
 *
 * ⚠️ WHY THE FIX IS A GUARD AND NOT A DIFFERENT coilBufferSize(). Dropping the
 * `if (nb <= 0) return 0;` clamp does NOT bring back a diagnostic: (nb + 7) / 8
 * truncates TOWARDS ZERO, so it is 0 for every nb in [-8, 0] and only goes
 * negative - and only then throws - at nb <= -9. Measured. The clamp is not
 * what is wrong; allocating on an unchecked count is.
 *
 * These cases execute the SHIPPED predicate through the seam above. The call
 * sites in WagoCtrl.cpp cannot be executed (is_connected() again) and are
 * covered by the source tripwire at the bottom of this file, as everything else
 * in WagoCtrl.cpp is.
 ******************************************************************************/

//⭐ The rule itself. Zero is not a smaller read, it is not a read; a negative
//count is not a count at all. Both are refused BEFORE anything is allocated.
TEST(WagoBits, ANonPositiveCountIsNotReadable)
{
    EXPECT_FALSE(seam::countIsReadable(0));
    EXPECT_FALSE(seam::countIsReadable(-1));
    EXPECT_FALSE(seam::countIsReadable(-8));
    EXPECT_FALSE(seam::countIsReadable(-40));

    EXPECT_TRUE(seam::countIsReadable(1));
    EXPECT_TRUE(seam::countIsReadable(FX_NB));
    EXPECT_TRUE(seam::countIsReadable(512));
}

//⭐ THE INVARIANT F1 IS ABOUT, stated over the two functions at once rather
//than over a hand-picked value: no count that read_bits() ACCEPTS may size a
//zero-byte buffer. The heap overflow needs both halves - an accepted count AND
//an empty allocation - so pinning the conjunction is what closes it.
TEST(WagoBits, NoReadableCountEverSizesAZeroByteBuffer)
{
    for (int nb = -64; nb <= 512; nb++)
    {
        if (!seam::countIsReadable(nb)) continue;
        EXPECT_GT(seam::coilBufferSize(nb), 0)
            << "nb = " << nb << " is accepted for reading and allocates nothing";
    }
}

//The two directions of the same wire must not disagree on what a count is: the
//writes already refuse nb <= 0 through countIsWritable(). Without this case the
//read guard could be written as `nb < 0` and the zero half would stay open.
TEST(WagoBits, TheReadAndWriteDirectionsAgreeOnWhatACount)
{
    const size_t plenty = 1024;

    for (int nb = -64; nb <= 512; nb++)
        EXPECT_EQ(seam::countIsWritable(nb, plenty), seam::countIsReadable(nb))
            << "nb = " << nb << ": the two directions of the wire disagree";
}

/*******************************************************************************
 * THE ACQUIRED - what already worked must not move
 ******************************************************************************/

//The one shape that was always right: eight bits, one byte, no boundary
//crossed. It is green in the characterization commit and must stay green,
//which is also what tells a uniformly-red run from a real one.
TEST(WagoBits, EightBitsStillPackAsBefore)
{
    const bool raw[8] = { false, true, false, false, true, true, false, true };
    vector<unsigned char> out = dirtyBuffer();
    ASSERT_TRUE(seam::packBits(8, vector<bool>(raw, raw + 8), out));
    ASSERT_EQ(1u, out.size()) << hex(out);
    EXPECT_EQ(0xb2, (int)out[0]) << hex(out);
}

/*******************************************************************************
 * THE TWIN - write_multiple_words, same guard, no bit packing
 ******************************************************************************/

//The word path has no setBit and no memset, so (b) and (c) do not apply to it;
//(a) does, identically. Empty vector<UWord>, count of 3: data() is null.
//⚠️ This case DIES in the characterization commit too.
TEST(WagoBits, AnEmptyWordVectorWithANonZeroCountIsRefused)
{
    vector<unsigned short> out;
    EXPECT_FALSE(seam::copyValues(3, vector<unsigned short>(), out));
    EXPECT_TRUE(out.empty());
}

TEST(WagoBits, AWordCountLargerThanTheValuesIsRefused)
{
    const unsigned short raw[2] = { 0x1234, 0xbeef };
    vector<unsigned short> out;
    EXPECT_FALSE(seam::copyValues(5, vector<unsigned short>(raw, raw + 2), out));
    EXPECT_TRUE(out.empty());
}

//Acquired for the twin: the values that ARE there are copied in order, and a
//count below the size copies exactly that many. The four values are pairwise
//distinct and not sorted, so a reversed or shifted copy is visible.
TEST(WagoBits, WordsAreCopiedInOrderUpToTheCount)
{
    const unsigned short raw[4] = { 0x00ff, 0x1234, 0x0001, 0xbeef };
    vector<unsigned short> out;
    ASSERT_TRUE(seam::copyValues(3, vector<unsigned short>(raw, raw + 4), out));
    ASSERT_EQ(3u, out.size());
    EXPECT_EQ(0x00ff, out[0]);
    EXPECT_EQ(0x1234, out[1]);
    EXPECT_EQ(0x0001, out[2]);
}

/*******************************************************************************
 * CALL-SITE TRIPWIRES - the only cover WagoCtrl.cpp itself can get
 *
 * WagoCtrl.o is in no test binary (it needs a live modbus socket to get past
 * is_connected()), so a mutation of WagoCtrl.cpp is not even COMPILED by this
 * suite. These two cases read the SHIPPED source and pin the shape of the call
 * sites, which is weaker than executing them and is labelled as such. They say
 * nothing about `address` and `nb` being swapped: that needs distinct types,
 * and it is T3.31 that owns it.
 ******************************************************************************/

TEST(WagoBits, ShippedWagoCtrlDelegatesTheBitPackingInsteadOfDoingItByHand)
{
    const string src = readShippedSource(
        "src/bin/calaos_server/IO/Wago/WagoCtrl.cpp");
    ASSERT_FALSE(src.empty()) << "cannot read the shipped WagoCtrl.cpp";

    //the one-byte setBit and its `*data` call site are gone
    EXPECT_EQ(string::npos, src.find("setBit(*data"))
        << "WagoCtrl.cpp still writes every bit into data[0]";
    EXPECT_EQ(string::npos, src.find("unsigned char &mot"))
        << "the byte-and-position setBit() signature is back, and it invites "
           "exactly the bug this ticket removed";

    //and the packing is delegated to the header this suite executes
    EXPECT_NE(string::npos, src.find("WagoBits.h"))
        << "WagoCtrl.cpp no longer includes the packer this suite covers";
    EXPECT_NE(string::npos, src.find("WagoBits::packBits"))
        << "write_multiple_bits() no longer calls the packer this suite covers";
    EXPECT_NE(string::npos, src.find("WagoBits::copyValues"))
        << "write_multiple_words() no longer calls the guard this suite covers";
}

TEST(WagoBits, ShippedWagoCtrlHasNoShortMemsetAndNoCeilingByHand)
{
    const string src = readShippedSource(
        "src/bin/calaos_server/IO/Wago/WagoCtrl.cpp");
    ASSERT_FALSE(src.empty()) << "cannot read the shipped WagoCtrl.cpp";

    //`nb / 8 + nb % 8` in any spacing - read_bits() carried it too
    string compact;
    for (size_t i = 0; i < src.size(); i++)
        if (src[i] != ' ' && src[i] != '\t') compact += src[i];

    EXPECT_EQ(string::npos, compact.find("nb/8+nb%8"))
        << "the over-allocating coil buffer expression is back in WagoCtrl.cpp";
    EXPECT_EQ(string::npos, compact.find("memset(data,'\\0',nb/8)"))
        << "the short memset is back in WagoCtrl.cpp";
}

//⭐ F1 - THE CALL SITES OF THE READ GUARD. The predicate above proves the RULE;
//these two prove read_bits() and read_words() actually ask it, and ask it
//BEFORE they allocate. Same weakness as the two tripwires above and labelled
//the same way: they read the shipped source, they do not execute it.
//
//⚠️ ONE CASE PER READ, deliberately. Written as a single case, losing the guard
//in read_bits() and losing it in read_words() produced the SAME red set and the
//log could not say which - measured, mutations M1 and M2 of this ticket.
//
//⚠️ The searched needle is the WHOLE STATEMENT, not just the call: a tripwire
//matching `WagoBits::countIsReadable(nb)` alone would be satisfied by a COMMENT
//mentioning it. The allocation needles carry the same hazard the other way -
//writing `new mbus_uword[nb]` inside a comment ABOVE the guard makes the
//allocation appear first and reddens the case. Both were observed while writing
//this, neither is theoretical.
namespace
{
const char *GUARD_STATEMENT = "if(!WagoBits::countIsReadable(nb))returnfalse;";

string compactedWagoCtrl()
{
    const string src = readShippedSource(
        "src/bin/calaos_server/IO/Wago/WagoCtrl.cpp");
    string compact;
    for (size_t i = 0; i < src.size(); i++)
        if (src[i] != ' ' && src[i] != '\t') compact += src[i];
    return compact;
}
} //anonymous namespace

TEST(WagoBits, ShippedReadBitsRefusesANonPositiveCountBeforeAllocating)
{
    const string compact = compactedWagoCtrl();
    ASSERT_FALSE(compact.empty()) << "cannot read the shipped WagoCtrl.cpp";

    const size_t fn = compact.find("boolWagoCtrl::read_bits(");
    ASSERT_NE(string::npos, fn) << "read_bits() is gone from WagoCtrl.cpp";
    const size_t alloc = compact.find("newmbus_ubyte[", fn);
    ASSERT_NE(string::npos, alloc)
        << "read_bits() no longer allocates; this tripwire needs rewriting";

    EXPECT_LT(compact.find(GUARD_STATEMENT, fn), alloc)
        << "read_bits() allocates a coil buffer on a count it never checked - "
           "a non-positive nb gives new mbus_ubyte[0] and libmbus then copies "
           "up to 255 response bytes into it";
}

TEST(WagoBits, ShippedReadWordsRefusesANonPositiveCountBeforeAllocating)
{
    const string compact = compactedWagoCtrl();
    ASSERT_FALSE(compact.empty()) << "cannot read the shipped WagoCtrl.cpp";

    const size_t fn = compact.find("boolWagoCtrl::read_words(");
    ASSERT_NE(string::npos, fn) << "read_words() is gone from WagoCtrl.cpp";
    const size_t alloc = compact.find("newmbus_uword[", fn);
    ASSERT_NE(string::npos, alloc)
        << "read_words() no longer allocates; this tripwire needs rewriting";

    EXPECT_LT(compact.find(GUARD_STATEMENT, fn), alloc)
        << "read_words() allocates a register buffer on a count it never "
           "checked - libmbus then writes up to 127 response words into it";
}
