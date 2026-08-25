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
