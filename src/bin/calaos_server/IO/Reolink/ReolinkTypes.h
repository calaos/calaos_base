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
#ifndef S_REOLINK_TYPES_H
#define S_REOLINK_TYPES_H

#include <string>
#include <utility>

/*
 * T3.31 - the four camera fields, each with a type of its own.
 *
 * There are TWO chains here, not one, and the second was missed by the first
 * pass of this ticket - it took a reviewer's mutation to find it.
 *
 * (1) REGISTRATION, outbound. Four hops, four adjacent std::string:
 *   ReolinkInputSwitch.cpp  ->  ReolinkCtrl::registerCamera()
 *                           ->  ReolinkEventRegistry::CameraRegistration
 *                           ->  ReolinkCtrl::doRegisterCamera()
 *                           ->  ReolinkWire::buildRegisterMessage()
 * Every one of those hops used to accept the four in ANY order, with no
 * diagnostic of any kind. The consequence is the worst of the T3.31 series:
 * the JSON is perfectly well formed, the camera silently fails to
 * authenticate, and the password goes out IN CLEAR IN THE USERNAME FIELD.
 * E4.1i measured that a swap at ReolinkCtrl.cpp leaves ReolinkWire_test
 * 17/17 GREEN - the call sites are not badly covered, they are absent from
 * the net by construction (the singleton spawns a process from its
 * constructor, so no test can build one).
 *
 * (2) ⭐ EVENT, inbound - and this one is WORSE than it looks:
 *   ReolinkCtrl.cpp messageReceived  ->  ReolinkEventRegistry::dispatch()
 *                                    ->  EventCallback
 *                                    ->  ReolinkInputSwitch::eventReceivedCallback()
 * plus cameraKey()/hasCamera()/callbackCount(), each taking hostname and
 * event_type as two bare adjacent std::string. MEASURED by the reviewer of
 * this ticket, on the first delivery, which had left this half untyped:
 *   - cameraKey(reg.event_type, reg.hostname)                  -> COMPILED, rc=0
 *   - dispatch(event_type, hostname, event_data)               -> COMPILED, rc=0
 * and no test caught either, because no test can: `nm` finds ZERO ReolinkCtrl
 * symbol in any of the test binaries. The second one is the dangerous shape -
 * a swapped dispatch() builds a key nothing is registered under, so it
 * returns 0 and EVERY EVENT FROM EVERY CAMERA IS SILENTLY DROPPED. No error,
 * no log, no failing test: the installation simply stops reacting.
 *
 * ⭐ cameraKey() has a second overload taking the CameraRegistration whole.
 * That is not sugar: an internal caller that already holds a registration
 * passes ONE argument, so there is no order left to get wrong AND no wrapping
 * to get wrong either (residual W3 below). The two-argument typed overload is
 * for callers that only have the two values, and it is the only place in the
 * class where a std::string is wrapped.
 *
 * ⭐ THE SHAPE MATTERS, and the two properties below are the whole point.
 * They are not decoration:
 *
 *   1. ONE field. A wrapper with two fields is a smaller version of the same
 *      problem; with one there is no order left to get wrong.
 *
 *   2. `explicit`. Without it a bare std::string converts into the wrapper on
 *      its own and the permutation type-checks again exactly as before -
 *      MEASURED, T3.31.md section 7.3, workaround W1. `explicit` also rejects
 *      copy-list-initialisation at the call site, f({a}, {b}), so the short
 *      spelling cannot creep back in either (workaround W5).
 *
 * ⚠️ These types do NOT make the registration correct. Username(password)
 * type-checks and always will (workaround W3, INTRINSIC). What they buy is
 * that the four hops collapse into ONE place where a human names the value -
 * and that place is a single line, right next to the field it names.
 *
 * ⛔ Do NOT give these an implicit conversion operator back to std::string
 * "for convenience". Measured (workaround W6): it re-arms every permutation
 * the explicit constructor just closed, because the wrappers then convert
 * freely into any std::string parameter, in any order.
 */
namespace ReolinkTypes
{

struct Hostname
{
    std::string v;
    explicit Hostname(std::string s): v(std::move(s)) {}
};

struct Username
{
    std::string v;
    explicit Username(std::string s): v(std::move(s)) {}
};

struct Password
{
    std::string v;
    explicit Password(std::string s): v(std::move(s)) {}
};

struct EventType
{
    std::string v;
    explicit EventType(std::string s): v(std::move(s)) {}
};

/* The payload of one camera event, as it comes off the JSON. Never a key,
 * never a name: it is the only one of the five that is pure data. It exists
 * so that the EVENT path has no two adjacent std::string left either - see
 * the second chain in the comment above. */
struct EventData
{
    std::string v;
    explicit EventData(std::string s): v(std::move(s)) {}
};

} //namespace ReolinkTypes

#endif
