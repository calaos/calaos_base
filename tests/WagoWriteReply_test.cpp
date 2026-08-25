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

/*----------------------------------------------------------------------------
 * T3.46 - THE WRITE HALF OF THE WAGO *REPLY* PATH.
 *
 * T3.31 typed the OUTBOUND half of the Wago chain (WagoMap::write_*,
 * WagoWire::build*, WagoCtrl::write_*). The replies come back the other way,
 * through four bare sigc::slot of scalars declared at WagoMap.h:38-41, and
 * NONE of them was typed. This suite is about the two that carry a WRITE
 * acknowledgement:
 *
 *     SingleBit_cb   (bool status, UWord address, bool  value)   MBUS_WRITE_BIT
 *     SingleWord_cb  (bool status, UWord address, UWord value)   MBUS_WRITE_WORD
 *
 * SingleWord_cb carries TWO Utils::UWord in a row: address and value are
 * interchangeable with no diagnostic whatsoever. That is literally the pair
 * F-WAGO-7 names, arriving instead of leaving. SingleBit_cb is the same shape
 * one type away - bool and UWord convert into one another in silence, and it
 * additionally puts `status` and `value` at positions 1 and 3 as two plain
 * bool, a second permutable pair in the same signature.
 *
 * ⚠️ WHAT KIND OF ORACLE THIS IS - read this before quoting any result below.
 *
 * These cases do NOT test behaviour and must never be sold as if they did.
 * Not one of the four implementations of these two slots is reachable from
 * any test binary: WOAnalog.cpp, WODigital.cpp and WagoIOBase.h are server
 * objects that this suite does not link, and WagoMap is not constructible
 * without opening a UDP socket and spawning calaos_wago. What these cases ask
 * is a question the COMPILER answers - "can a caller still hand this slot its
 * arguments in the wrong order?" - and std::is_invocable_v turns that answer
 * into a value gtest can print. It is a COMPILATION property reported through
 * an executable oracle, the shape T3.31 used and that was accepted. It proves
 * a permuted call no longer TYPE-CHECKS. It proves nothing about what any
 * callback then does.
 *
 * The closure of the four IMPLEMENTATIONS themselves is not asked here at
 * all: it is established by mutation, by permuting the two parameters in each
 * of the four signatures and watching the build REFUSE. See
 * docs/refactoring/T3.46.md section 7.
 *
 * ⭐ AND ONE DISTINCTION THAT MATTERS, because getting it wrong means asking a
 * test to tell two identical programs apart:
 *
 *   - WOAnalog::WagoWriteCallback (WOAnalog.cpp:86) does `value = _value` and
 *     emitChange(). Permuting its two parameters CHANGES WHAT THE PROGRAM
 *     DOES: the analog output would report the modbus address it just wrote
 *     to as its own value.
 *   - WODigital::WagoWriteCallback (WODigital.cpp:109) and
 *     WOVoletBase::WagoWriteCallback (WagoIOBase.h:291) read `status` and
 *     NOTHING ELSE. Their two other parameters are unused. Permuting them is
 *     SEMANTICALLY A NO-OP - the two programs are the same program, and no
 *     behavioural test could ever separate them. Typing them still closes a
 *     real hole, but the hole is a CONTRACT for whoever writes the next
 *     implementation or the next emission site, not a live wrong answer.
 *   - OutputAnalog::WagoWriteCallback (OutputAnalog.h:42) has no definition
 *     anywhere in the tree. Measured, not assumed: zero occurrences of
 *     `OutputAnalog::WagoWriteCallback` outside a comment.
 *
 * ⚠️ And the emission side, which is where the live value actually is:
 * WagoMap.cpp:219 sends the literal `false` and WagoMap.cpp:242 the literal
 * `0` as the "value" of a write acknowledgement. The VALUE at the only site
 * that sends one is therefore a dummy - T3.31 measured that and it is why the
 * reads were meant to go first. The ADDRESS at those two lines is NOT a
 * dummy: it is decoded from the reply. So a permutation there does not
 * substitute one dummy for another, it sends the LIVE address where the value
 * belongs, and WOAnalog assigns it. The write half is worth less than the
 * read half; it is not worth nothing.
 *
 * ⚠️ WHAT THIS DOES NOT CLOSE, declared and not promised:
 *   W3 - wrapping the WRONG variable. WagoTypes::WordValue(address) at
 *        WagoMap.cpp type-checks and always will. Measured as a residual.
 *   W7 - unwrapping into an untyped layer. `value = addr.v` instead of
 *        `value = _value.v` inside WOAnalog::WagoWriteCallback type-checks.
 *        One line per implementation, and it is where the wrapper is undone.
 *
 * ⛔ NOT FIXED HERE, on purpose: WOAnalog::WagoWriteCallback overwrites its
 * own reported value with that literal 0 after every successful write
 * (T3.46.md section 6.4). That is a BEHAVIOUR change on untouched master code
 * and needs its own arbitration.
 *--------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <type_traits>

#include "WagoMap.h"
#include "WagoTypes.h"

using Calaos::SingleBit_cb;
using Calaos::SingleWord_cb;

/* ⭐ THE RED ONE: two Utils::UWord in a row, on a write acknowledgement. */
TEST(WagoWriteReply, TheSingleWordReplyRefusesABareAddressAndValuePair)
{
    EXPECT_FALSE((std::is_invocable_v<SingleWord_cb, bool, UWord, UWord>))
        << "SingleWord_cb still takes two bare UWord: at WagoMap.cpp:242 the "
           "address and the value of a write reply are interchangeable in "
           "total silence - this is the pair F-WAGO-7 names, coming back";

    //The counterpart. Without it, a slot that accepted NOTHING would pass the
    //case above for free.
    EXPECT_TRUE((std::is_invocable_v<SingleWord_cb, bool,
                                     WagoTypes::Address, WagoTypes::WordValue>))
        << "the correctly ordered typed call must still be accepted";

    //⭐ And the permutation OF THE TYPED FORM, which is the whole point.
    EXPECT_FALSE((std::is_invocable_v<SingleWord_cb, bool,
                                      WagoTypes::WordValue, WagoTypes::Address>))
        << "address and value are still interchangeable once typed";
}

/* The bit reply: (bool status, UWord address, bool value). Two permutable
 * pairs in one signature - address/value across two convertible widths, and
 * status/value which are both plain bool. */
TEST(WagoWriteReply, TheSingleBitReplyRefusesABareAddressAndValuePair)
{
    EXPECT_FALSE((std::is_invocable_v<SingleBit_cb, bool, UWord, bool>))
        << "SingleBit_cb still takes a bare UWord and a bare bool: they "
           "convert both ways, so (status, value, address) type-checks";

    EXPECT_FALSE((std::is_invocable_v<SingleBit_cb, bool, bool, UWord>))
        << "the permuted spelling of the same list still type-checks";

    EXPECT_TRUE((std::is_invocable_v<SingleBit_cb, bool,
                                     WagoTypes::Address, WagoTypes::BitValue>));

    EXPECT_FALSE((std::is_invocable_v<SingleBit_cb, bool,
                                      WagoTypes::BitValue, WagoTypes::Address>))
        << "address and value are still interchangeable once typed";

    //status and value are BOTH bool at positions 1 and 3. Typing the payload
    //is what separates them; nothing else in the signature does.
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::BitValue, bool>))
        << "a BitValue that converts back to bool re-arms the status/value "
           "permutation this signature also carries";
}

/* T3.46 - the shape of the wrappers, asked of the type system.
 *
 * The two cases above would still pass if someone "simplified" WagoTypes into
 * non-explicit wrappers, gave them a common base, or added a conversion
 * operator back to the scalar. These probe the properties that make the
 * closure hold (docs/refactoring/T3.31.md section 7.3, workarounds W1/W4/W6).
 * They overlap on purpose with WagoWire_test: that suite links a different
 * set of objects and neither one covers the other. */
TEST(WagoWriteReply, TheWrapperShapeIsWhatCloses)
{
    //W1 - explicit constructors, so a bare scalar is not an Address.
    EXPECT_FALSE((std::is_convertible_v<UWord, WagoTypes::Address>));
    EXPECT_FALSE((std::is_convertible_v<UWord, WagoTypes::WordValue>));
    EXPECT_FALSE((std::is_convertible_v<bool, WagoTypes::BitValue>));
    EXPECT_TRUE((std::is_constructible_v<WagoTypes::Address, UWord>));

    //W4 - no common base, so no sibling stands in for another.
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::WordValue, WagoTypes::Address>));
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Address, WagoTypes::WordValue>));
    EXPECT_FALSE((std::is_base_of_v<WagoTypes::Address, WagoTypes::WordValue>));

    //W6 - and no way back to the raw scalar without naming .v.
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::Address, UWord>));
    EXPECT_FALSE((std::is_convertible_v<WagoTypes::WordValue, UWord>));
}

namespace
{

/* Witnesses for the two BLIND SPOTS of this kind of probe, both measured by
 * T3.31 (F-TYPE-5) and both re-armed here rather than trusted from a note. */

struct Payload { int v; explicit Payload(int a): v(a) {} };
struct Slot { int v; explicit Slot(int a): v(a) {} };

/* Blind spot 1 - a NON-CONST LVALUE REFERENCE parameter. is_invocable_v with
 * prvalue arguments answers FALSE even for the CORRECT order, so a signature
 * written this way would make every probe above pass for the wrong reason,
 * while a permuted call written with real lvalues compiles fine. This is why
 * the four implementations T3.46 types take their payload BY VALUE. */
inline void takesLvalueRefs(Payload &, Slot &) { }

/* Blind spot 2 - NARROWING. Neither braces nor direct-initialisation
 * discriminate a narrowing conversion in a way is_constructible_v can see;
 * only is_convertible_v answers the question that matters. */
struct Narrowable { long v; Narrowable(long a): v(a) {} };

} //namespace

/* The probes must be able to answer TRUE, or every EXPECT_FALSE above is free.
 * This case is the witness for the whole file. */
TEST(WagoWriteReply, TheProbesActuallyDiscriminate)
{
    using Fn = decltype(&takesLvalueRefs);

    //A matching, correctly ordered list IS invocable - proved on the real
    //slots, not on a toy.
    EXPECT_TRUE((std::is_invocable_v<SingleWord_cb, bool,
                                     WagoTypes::Address, WagoTypes::WordValue>));

    //Blind spot 1, written down as a live measurement: with lvalue-reference
    //parameters the probe says FALSE for the CORRECT order too.
    EXPECT_FALSE((std::is_invocable_v<Fn, Payload, Slot>))
        << "if this ever becomes TRUE the lvalue-reference blind spot is gone "
           "and this comment is stale";
    Payload p(1); Slot s(2);
    EXPECT_TRUE((std::is_invocable_v<Fn, Payload &, Slot &>));
    (void)p; (void)s;

    //Blind spot 2: a narrowing, non-explicit constructor is STILL
    //convertible-from, which is the property that re-arms every permutation.
    //is_constructible_v cannot tell it apart from an explicit one.
    EXPECT_TRUE((std::is_constructible_v<Narrowable, int>));
    EXPECT_TRUE((std::is_convertible_v<int, Narrowable>))
        << "only is_convertible_v separates an explicit wrapper from a "
           "narrowing implicit one - is_constructible_v answers TRUE for both";
    EXPECT_TRUE((std::is_constructible_v<WagoTypes::Address, UWord>));
    EXPECT_FALSE((std::is_convertible_v<UWord, WagoTypes::Address>));
}

/*----------------------------------------------------------------------------
 * T3.46 part A - libmbus, THE LAST HOP, and why it stays untyped.
 *
 * The Calaos side of a single write is typed end to end now: WOAnalog ->
 * WagoMap -> WagoWire -> (JSON) -> WagoExternProc_main -> WagoCtrl, and the
 * reply comes back through the two slots above. The hop AFTER WagoCtrl is
 * not, and cannot be closed the same way:
 *
 *   mbus_cmd_force_single_coil     (mbus, slave, mbus_uword coil_addr,     mbus_uword data)
 *   mbus_cmd_preset_single_register(mbus, slave, mbus_uword register_addr, mbus_uword preset_data)
 *
 * ⭐ THE ARBITRATION, and the measurement that decides it. T3.31 refused to
 * patch libmbus on ownership and cost. That refusal is UPHELD here, on a
 * measurement T3.31 did not have and that makes it stronger rather than
 * merely repeated:
 *
 *   A one-field struct DOES close a permutation in C - re-measured for this
 *   ticket, gcc -std=c11 -Wall -Wextra: the correct order compiles (rc=0),
 *   the permuted typed pair and the bare permutation are both refused (rc=1).
 *   So this is not a technical impossibility, exactly as T3.31 said.
 *
 *   ⭐ But the permutable pair is NOT in the six public commands. All six are
 *   thin wrappers over ONE internal request builder,
 *   mbus_cmd_addr_wdata(mbus, slave_addr, funct_code, mbus_uword addr,
 *   mbus_uword data), and across its five call sites in mbus_cmd.c its two
 *   word parameters carry THREE different roles: an address + a COUNT
 *   (read_coil_status, read_holding_registers), an address + a DATA
 *   (force_single_coil, preset_single_register) and a SUBFUNCTION + a data
 *   (diagnostics). One-type-per-role therefore cannot be applied to it at
 *   all without SPLITTING it - a functional change to vendored 2003 C that
 *   nothing in this tree executes. Typing only the six public signatures
 *   leaves the same pair live one frame below and moves the six unwraps from
 *   WagoCtrl.cpp into libmbus: T3.31's "seven places to seven places, no net
 *   gain", now measured one level deeper with a concrete reason.
 *
 *   And "nothing executes it" is measured, not assumed: over the 92 ELF
 *   binaries under tests/, mbus_cmd_preset_single_register,
 *   mbus_cmd_force_single_coil and mbus_cmd_addr_wdata are defined in ZERO
 *   and referenced as undefined in ZERO.
 *
 * ⇒ The residual is DECLARED, not deferred a third time. What follows is the
 * other half of that sentence: since the six call sites cannot be closed by
 * type, the two WRITE ones - the pair F-WAGO-7 names, the ones where a
 * permutation drives a relay or presets a register at an arbitrary address -
 * are pinned by a SOURCE TRIPWIRE instead, the mechanism JanssonResidues_test
 * and the T3.28 suites already use over CALAOS_TOP_SRCDIR.
 *
 * ⚠️ THIS IS NOT A TYPE. It cannot stop a NEW call site from being written
 * with the arguments the wrong way round; it only fails when THESE two lines
 * change. That is strictly less than the C++ side gets, and saying otherwise
 * would be selling an oracle for something it is not.
 *
 * If it fires: someone edited WagoCtrl.cpp:write_single_bit or
 * :write_single_word. Check that the modbus ADDRESS is still the third
 * argument and the payload the fourth, then update the expectation here.
 *--------------------------------------------------------------------------*/

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::string readSource(const std::string &rel)
{
    std::ifstream f(std::string(CALAOS_TOP_SRCDIR) + "/" + rel);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* The comma-separated argument list of the first call to `fn`, with every run
 * of whitespace squeezed to one space and casts left in place. Depth-aware, so
 * a nested call or a cast never splits an argument in two. */
std::vector<std::string> argumentsOf(const std::string &src, const std::string &fn)
{
    std::vector<std::string> args;
    size_t call = src.find(fn + "(");
    if (call == std::string::npos) return args;

    size_t i = call + fn.size() + 1;
    int depth = 0;
    std::string cur;
    for (; i < src.size(); i++)
    {
        char c = src[i];
        if (c == '(') depth++;
        else if (c == ')')
        {
            if (depth == 0) { args.push_back(cur); break; }
            depth--;
        }
        if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); continue; }
        cur += c;
    }

    for (std::string &a: args)
    {
        std::string out;
        bool sp = false;
        for (char c: a)
        {
            if (isspace(static_cast<unsigned char>(c))) { sp = !out.empty(); continue; }
            if (sp) { out += ' '; sp = false; }
            out += c;
        }
        a = out;
    }
    return args;
}

} //namespace

/* ⚠️ A SOURCE TRIPWIRE, not a type. See the block above for what it does and
 * does not buy. */
TEST(WagoWriteReply, TheTwoUntypedLibmbusWriteCallsStillPassAddressBeforePayload)
{
    const std::string src = readSource("src/bin/calaos_server/IO/Wago/WagoCtrl.cpp");
    ASSERT_FALSE(src.empty())
        << "WagoCtrl.cpp not readable under CALAOS_TOP_SRCDIR - the tripwire "
           "cannot be green just because it read nothing";

    const std::vector<std::string> coil =
        argumentsOf(src, "mbus_cmd_force_single_coil");
    ASSERT_EQ(4u, coil.size()) << "mbus_cmd_force_single_coil() call not found "
                                 "with four arguments in WagoCtrl.cpp";
    EXPECT_NE(std::string::npos, coil[2].find("address"))
        << "3rd argument of mbus_cmd_force_single_coil() is [" << coil[2]
        << "], expected the modbus ADDRESS - a permutation here forces a "
           "physical relay at whatever the payload happened to be";
    EXPECT_NE(std::string::npos, coil[3].find("data"))
        << "4th argument of mbus_cmd_force_single_coil() is [" << coil[3] << "]";

    const std::vector<std::string> reg =
        argumentsOf(src, "mbus_cmd_preset_single_register");
    ASSERT_EQ(4u, reg.size()) << "mbus_cmd_preset_single_register() call not "
                                "found with four arguments in WagoCtrl.cpp";
    EXPECT_NE(std::string::npos, reg[2].find("address"))
        << "3rd argument of mbus_cmd_preset_single_register() is [" << reg[2]
        << "], expected the modbus ADDRESS";
    EXPECT_NE(std::string::npos, reg[3].find("val"))
        << "4th argument of mbus_cmd_preset_single_register() is [" << reg[3] << "]";
}

/* The tripwire above would pass on an empty file, on a renamed function, or
 * on a parser that always answers "found". This is its witness: the same
 * parser, on a call whose order is deliberately wrong, must disagree. */
TEST(WagoWriteReply, TheSourceTripwireCanActuallyFail)
{
    const std::string wrong =
        "int ret = mbus_cmd_preset_single_register(mbus, 1, val, (mbus_uword)address);";
    const std::vector<std::string> a =
        argumentsOf(wrong, "mbus_cmd_preset_single_register");
    ASSERT_EQ(4u, a.size());
    EXPECT_EQ(std::string::npos, a[2].find("address"))
        << "the parser cannot tell a permuted call apart - the tripwire above "
           "is worthless";
    EXPECT_NE(std::string::npos, a[3].find("address"));

    //And it must not answer four arguments for something that is not there.
    EXPECT_TRUE(argumentsOf(wrong, "mbus_cmd_force_single_coil").empty());
    //Nested parentheses must not split an argument.
    const std::vector<std::string> nested =
        argumentsOf("f(a, g(b, c), d)", "f");
    ASSERT_EQ(3u, nested.size());
    EXPECT_EQ("g(b, c)", nested[1]);
}
