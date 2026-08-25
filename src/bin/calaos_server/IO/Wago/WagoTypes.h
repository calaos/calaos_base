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
 * ⭐ THE SHAPE, and why it is this shape (all four measured, T3.31 section 3):
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
 *     See docs/refactoring/T3.31.md section 4.
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
