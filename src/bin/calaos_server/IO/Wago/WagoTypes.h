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
#ifndef S_WAGO_TYPES_H
#define S_WAGO_TYPES_H

#include <Utils.h>

/*
 * T3.31 - what a Wago modbus call carries, one type per role.
 *
 * A single word write travels four hops before it reaches the PLC:
 *   WOAnalog.cpp     ->  WagoMap::write_single_word(address, val)
 *                    ->  WagoWire::buildWriteWordRequest(id, address, value)
 *   --- JSON, calaos_server -> calaos_wago ---
 *   WagoExternProc_main.cpp ->  WagoCtrl::write_single_word(address, val)
 *                           ->  mbus_cmd_preset_single_register(...)
 * and until this ticket ALL FOUR took the address and the payload as two
 * values of the same width, in a row, exchangeable with no diagnostic. That
 * is the pair F-WAGO-7 names, and T3.43 section 5.5bis rates it red: a
 * permutation does not read the wrong register, it WRITES - it drives a
 * physical relay, or presets a register, at whatever address the value
 * happened to be.
 *
 * The reads carry (UWord address, int count), which is not the same type but
 * is just as permutable: the two convert into one another in silence. E4.1h
 * measured it - swapping them at WagoMap.cpp left WagoWire_test 31/31 GREEN
 * with no warning at all.
 *
 * ⭐ THE SHAPE, and why it is this shape (all four measured, T3.31.md section 7.3):
 *
 *   1. ONE field each. A wrapper with two fields is a smaller copy of the
 *      same problem. With one there is no order left to get wrong.
 *
 *   2. `explicit`. Without it a bare UWord or a bare int converts into the
 *      wrapper on its own and every permutation type-checks again exactly as
 *      before (workaround W1). `explicit` also rejects f({a}, {b}) at the
 *      call site (workaround W5).
 *
 *   3. NO common base. Sibling wrappers that derive from one base convert
 *      INTO that base, so any signature written in terms of the base takes
 *      them interchangeably (workaround W4). These four are unrelated types.
 *
 * ⛔ Do NOT add `operator Utils::UWord()` "for convenience". Measured
 * (workaround W6): it re-arms every permutation the explicit constructor just
 * closed, and it silently re-opens the untyped call into libmbus as well.
 *
 * ⚠️ WHAT THIS DOES NOT CLOSE, declared and not promised:
 *
 *   - Wrapping the WRONG variable. WagoTypes::Address(val) type-checks and
 *     always will (workaround W3). This is INTRINSIC to wrapping: the typing
 *     collapses four unguarded hops into ONE place where a human names the
 *     value, it does not remove that place.
 *
 *   - The last hop, into libmbus. mbus_cmd_preset_single_register() and
 *     mbus_cmd_force_single_coil() take two mbus_uword in a row, and a
 *     permutation there writes to an arbitrary register of the PLC.
 *     ⚠️ It is NOT closed here, and the reason is a cost decision, not a
 *     technical impossibility - that distinction was measured and it matters:
 *     a one-field `struct` closes a permutation in C exactly as it does in
 *     C++ (gcc -std=c11: "incompatible type for argument 1"). What stops it
 *     is ownership. libmbus is a vendored third-party import
 *     ($Id: mbus_conf.h,v 1.1.1.1 2003/...) already diverged from upstream,
 *     the change would touch all seven command signatures across four C
 *     files, and nothing in this tree executes libmbus, so it would be
 *     verified by compilation alone. T3.43 section 3 refused the same change
 *     for the same reason. WagoCtrl.cpp therefore unwraps with `.v` in
 *     exactly one line per command, right under the parameter it names.
 *     See docs/refactoring/T3.31.md section 7.5.
 *
 *   - THE RETURN PATH, HALF of which is now closed - T3.46.
 *     Everything above is the OUTBOUND half. The replies come back through
 *     four sigc::slot declared at WagoMap.h, and they carry the same
 *     permutable pairs the other way round.
 *
 *     CLOSED by T3.46, the WRITE half - two slots, four implementations, two
 *     emission sites:
 *       SingleBit_cb   (bool, WagoTypes::Address, WagoTypes::BitValue)
 *       SingleWord_cb  (bool, WagoTypes::Address, WagoTypes::WordValue)
 *       WOAnalog::WagoWriteCallback      (WOAnalog.cpp)
 *       WODigital::WagoWriteCallback     (WODigital.cpp)
 *       WOVoletBase::WagoWriteCallback   (WagoIOBase.h)
 *       WagoMap::processNewMessage, the two singleBit_cb/singleWord_cb calls
 *       OutputAnalog::WagoReadCallback / ::WagoWriteCallback - DELETED, they
 *         were defined nowhere in the tree (measured), which is what keeps
 *         WagoTypes:: out of a generic base with no business knowing Wago.
 *     Every one of those parameters is taken BY VALUE, not by lvalue
 *     reference: a non-const reference makes std::is_invocable_v answer false
 *     for the CORRECT order too, and the probes would pass for the wrong
 *     reason (F-TYPE-5).
 *
 *     STILL OPEN, the READ half - two slots and seven implementations:
 *       MultiBits_cb / MultiWords_cb  (bool, UWord address, int count, ...)
 *       WagoMap::WagoModbusReadHeartbeatCallback  (WagoMap.cpp)
 *       WIAnalog::WagoReadCallback                (WIAnalog.cpp)
 *       WITemp::WagoReadCallback                  (WITemp.cpp)
 *       WOAnalog::WagoReadCallback                (WOAnalog.cpp)
 *       WODigital::WagoReadCallback               (WODigital.cpp)
 *       WIDigitalBase::WagoReadCallback           (WagoIOBase.h)
 *       plus four invocation sites in WagoMap::processNewMessage.
 *     Their (UWord address, int count) is the pair E4.1h measured GREEN on a
 *     swap. It is the MORE dangerous half - see below - and it is left for
 *     the read ticket, on scope, not on difficulty.
 *
 *     Measured, and it is why T3.31 said the reads should go first - the two
 *     halves are NOT equally dangerous:
 *       - the READ callbacks carry (UWord address, int count) live and
 *         adjacent: the same pair E4.1h measured, and it is real data;
 *       - the WRITE callbacks carry (address, value) where the VALUE is a
 *         constant at the only place that sends it - WagoMap.cpp passes the
 *         literal `false` for a bit and the literal `0` for a word.
 *         Permuting them substitutes a dummy for a live payload.
 *       But not for nothing, which is what T3.46 measured on top: the ADDRESS
 *         at those same two lines is NOT a dummy, it is decoded from the
 *         reply. A permutation there hands the LIVE address over as the
 *         value, and WOAnalog assigns it.
 *     Consequence worth its own line, NOT fixed here because it is a
 *     behaviour change on untouched master code: WOAnalog::WagoWriteCallback
 *     assigns `value = _value.v`, so a Wago analog output overwrites its own
 *     reported value with that literal 0 after every successful write, and
 *     emitChange()s it. Reported, not changed. T3.46.md section 6.4.
 */
namespace WagoTypes
{

/* A modbus register or coil address, as it goes on the wire. */
struct Address
{
    Utils::UWord v;
    explicit Address(Utils::UWord a): v(a) {}
};

/* How many coils or registers a read or a multiple write covers. Signed, and
 * deliberately so: it arrives from the JSON through Utils::from_string() and
 * can be negative or absurd, which is what WagoBits::countIsReadable() and
 * countIsWritable() are there to refuse (T3.30). This type does not validate
 * anything - it only stops the value being mistaken for an address. */
struct Count
{
    int v;
    explicit Count(int c): v(c) {}
};

/* The payload of a single register write. */
struct WordValue
{
    Utils::UWord v;
    explicit WordValue(Utils::UWord w): v(w) {}
};

/* The payload of a single coil write. bool and UWord convert both ways, so
 * without this the address and the value of write_single_bit() were just as
 * exchangeable as the two UWord of write_single_word(). */
struct BitValue
{
    bool v;
    explicit BitValue(bool b): v(b) {}
};

} //namespace WagoTypes

#endif
