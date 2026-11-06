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

// T1.14 — Reolink callback registry: register/unregister bookkeeping and
// reentrant-safe dispatch. Regression coverage for the ReolinkInputSwitch
// use-after-free: before this ticket the ReolinkCtrl singleton had no
// unregister path at all, so a deleted IO (config reload) left a dangling
// std::function that fired on the next camera event.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ReolinkEventRegistry.h"

using std::string;

/* T3.31 - the event half of the chain is typed too, so the call sites below
 * name each value. Short aliases keep the pre-existing cases readable: NOT
 * ONE ASSERTION CHANGED, only the argument lists that feed them. */
using RH = ReolinkTypes::Hostname;
using RE = ReolinkTypes::EventType;
using RD = ReolinkTypes::EventData;

static ReolinkEventRegistry::CameraRegistration makeReg(const string &host, const string &evt)
{
    /* T3.31 - was `return {host, "user", "secret", evt};`, the same positional
     * brace-init ReolinkCtrl.cpp:100 wrote. The four values are now named on
     * their own line, and no other order compiles.
     *
     * ⚠️ Fixture: the four are mutually non-substitutable on purpose - a
     * hostname that looks like a hostname, a username and a password of
     * different lengths and different character families, and an event type
     * the caller chooses. A fixture whose fields could stand in for one
     * another would prove nothing about a permutation. */
    return ReolinkEventRegistry::CameraRegistration(
               ReolinkTypes::Hostname(host),
               ReolinkTypes::Username("operator-7"),
               ReolinkTypes::Password("Kx9!vQm2#Zt"),
               ReolinkTypes::EventType(evt));
}

TEST(ReolinkRegistry, CameraKeyFormat)
{
    EXPECT_EQ("10.0.0.5_motion", ReolinkEventRegistry::cameraKey(RH("10.0.0.5"), RE("motion")));
}

TEST(ReolinkRegistry, AddAndDispatchDeliversEvent)
{
    ReolinkEventRegistry reg;
    string gotHost, gotType, gotData;

    auto id = reg.add(makeReg("cam1", "motion"),
                      [&](const RH &h, const RE &t, const RD &d)
                      { gotHost = h.v; gotType = t.v; gotData = d.v; });
    ASSERT_NE(ReolinkEventRegistry::INVALID_ID, id);

    EXPECT_EQ(1u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ("cam1", gotHost);
    EXPECT_EQ("motion", gotType);
    EXPECT_EQ("ON", gotData);

    // Other hostname/event_type must not match
    EXPECT_EQ(0u, reg.dispatch(RH("cam1"), RE("person"), RD("ON")));
    EXPECT_EQ(0u, reg.dispatch(RH("cam2"), RE("motion"), RD("ON")));
}

// The UAF regression scenario: once an IO unregisters (destructor), its
// callback must never be invoked again.
TEST(ReolinkRegistry, RemovedCallbackNeverFiresAgain)
{
    ReolinkEventRegistry reg;
    int fired = 0;

    auto id = reg.add(makeReg("cam1", "motion"),
                      [&](const RH &, const RE &, const RD &) { fired++; });

    EXPECT_EQ(1u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(1, fired);

    EXPECT_TRUE(reg.remove(id)); //last callback: registration fully dropped
    EXPECT_FALSE(reg.hasCamera(RH("cam1"), RE("motion")));
    EXPECT_TRUE(reg.empty());

    EXPECT_EQ(0u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(1, fired);
}

TEST(ReolinkRegistry, TwoCallbacksSameCameraRemoveOneKeepsRegistration)
{
    ReolinkEventRegistry reg;
    int firedA = 0, firedB = 0;

    auto idA = reg.add(makeReg("cam1", "motion"),
                       [&](const RH &, const RE &, const RD &) { firedA++; });
    auto idB = reg.add(makeReg("cam1", "motion"),
                       [&](const RH &, const RE &, const RD &) { firedB++; });
    EXPECT_NE(idA, idB);
    EXPECT_EQ(2u, reg.callbackCount(RH("cam1"), RE("motion")));

    EXPECT_EQ(2u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));

    // Removing A is not the last one: the camera registration survives
    EXPECT_FALSE(reg.remove(idA));
    EXPECT_TRUE(reg.hasCamera(RH("cam1"), RE("motion")));
    EXPECT_EQ(1u, reg.callbackCount(RH("cam1"), RE("motion")));

    EXPECT_EQ(1u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(1, firedA);
    EXPECT_EQ(2, firedB);

    // Removing B is the last one, and it reports which camera key dropped
    string removedKey;
    EXPECT_TRUE(reg.remove(idB, &removedKey));
    EXPECT_EQ("cam1_motion", removedKey);
    EXPECT_TRUE(reg.empty());
}

TEST(ReolinkRegistry, RemoveUnknownOrInvalidIdIsNoop)
{
    ReolinkEventRegistry reg;
    EXPECT_FALSE(reg.remove(ReolinkEventRegistry::INVALID_ID));
    EXPECT_FALSE(reg.remove(12345));

    auto id = reg.add(makeReg("cam1", "motion"),
                      [](const RH &, const RE &, const RD &) {});
    EXPECT_TRUE(reg.remove(id));
    EXPECT_FALSE(reg.remove(id)); //double remove is safe
}

// A callback that unregisters another callback mid-dispatch (e.g. a rule
// fired by camera A deletes the IO holding camera B's callback) must
// prevent the unregistered callback from being invoked in that dispatch.
TEST(ReolinkRegistry, ReentrantRemoveDuringDispatchSkipsRemovedCallback)
{
    ReolinkEventRegistry reg;
    int firedB = 0;
    ReolinkEventRegistry::RegistrationId idB = ReolinkEventRegistry::INVALID_ID;

    reg.add(makeReg("cam1", "motion"),
            [&](const RH &, const RE &, const RD &)
            { reg.remove(idB); }); //first callback kills the second
    idB = reg.add(makeReg("cam1", "motion"),
                  [&](const RH &, const RE &, const RD &) { firedB++; });

    EXPECT_EQ(1u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(0, firedB); //B was unregistered mid-dispatch: never invoked
}

// A callback that registers a new camera mid-dispatch (config reload adds
// IOs) must not corrupt the iteration nor fire in the same dispatch.
TEST(ReolinkRegistry, ReentrantAddDuringDispatchIsSafe)
{
    ReolinkEventRegistry reg;
    int firedNew = 0;

    reg.add(makeReg("cam1", "motion"),
            [&](const RH &, const RE &, const RD &)
            {
                reg.add(makeReg("cam1", "motion"),
                        [&](const RH &, const RE &, const RD &) { firedNew++; });
            });

    EXPECT_EQ(1u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(0, firedNew); //not part of the dispatched snapshot

    //second dispatch snapshots [original, first-added]; the original adds
    //yet another callback which again only fires next time
    EXPECT_EQ(2u, reg.dispatch(RH("cam1"), RE("motion"), RD("ON")));
    EXPECT_EQ(1, firedNew);
}

TEST(ReolinkRegistry, ForEachRegistrationListsCrashRecoveryRecords)
{
    ReolinkEventRegistry reg;
    auto noop = [](const RH &, const RE &, const RD &) {};

    reg.add(makeReg("cam1", "motion"), noop);
    reg.add(makeReg("cam1", "motion"), noop); //same camera: one record
    auto id3 = reg.add(makeReg("cam2", "person"), noop);
    EXPECT_EQ(2u, reg.cameraCount());

    std::vector<string> seen;
    reg.forEachRegistration([&](const ReolinkEventRegistry::CameraRegistration &r)
    {
        seen.push_back(ReolinkEventRegistry::cameraKey(r));
    });
    ASSERT_EQ(2u, seen.size());

    EXPECT_TRUE(reg.remove(id3));
    EXPECT_EQ(1u, reg.cameraCount());
    EXPECT_TRUE(reg.hasCamera(RH("cam1"), RE("motion")));
    EXPECT_FALSE(reg.hasCamera(RH("cam2"), RE("person")));
}

/*----------------------------------------------------------------------------
 * T3.31 - CameraRegistration IS AN AGGREGATE, AND THAT IS THE WHOLE HOLE.
 *
 * The struct exists and carries exactly the four fields by name, and it has
 * closed NOTHING: ReolinkCtrl.cpp:100 builds it POSITIONALLY,
 * registry.add({hostname, username, password, event_type}, ...), so username
 * and password are still two adjacent std::string a caller can swap in
 * silence. The named struct bought a name, not a check.
 *
 * ⭐ MEASURED, and it contradicts what T3.31 asked for. The ticket's
 * acceptance criterion 3 says to close this "by giving CameraRegistration a
 * constructor". A constructor removes aggregate-ness, but the constructor is
 * ITSELF positional:
 *
 *     Reg(std::string h, std::string u, std::string p, std::string e);
 *     add({h, p, u, e});      // still compiles, zero warnings at -Wall
 *                             // -Wextra -Wconversion, username <-> password
 *
 * Only per-field STRONG TYPES close it, and that is what the two probes below
 * demand: not "is it an aggregate", but "can four bare strings still be
 * poured into it in any order at all", which is the question that matters and
 * the one a plain constructor still answers yes to.
 *
 * ⚠️ Compilation property reported through an executable oracle. It does not
 * close the residual of wrapping the wrong variable - Username(password)
 * type-checks and always will - it only collapses that risk from four call
 * sites down to the one line that does the wrapping.
 *--------------------------------------------------------------------------*/

namespace
{

/* Direct-initialisation, T obj(a, b, c, d). In C++20 this also reaches
 * parenthesised aggregate initialisation, so it is true for a bare aggregate
 * AND for a positional constructor - which is exactly why it is probed. */
template <class T, class... A>
constexpr bool isDirectInitializable = std::is_constructible_v<T, A...>;

/* List-initialisation, T{a, b, c, d} - the form ReolinkCtrl.cpp:100 actually
 * writes. std::is_constructible does not see this one, so it gets its own
 * detector. */
template <class T, class... A>
class BraceInitProbe
{
    template <class U, class... B>
    static auto probe(int) -> decltype(U{std::declval<B>()...}, std::true_type{});
    template <class, class...>
    static std::false_type probe(...);

public:
    static constexpr bool value = decltype(probe<T, A...>(0))::value;
};

template <class T, class... A>
constexpr bool isBraceInitializable = BraceInitProbe<T, A...>::value;

} //namespace

TEST(ReolinkRegistry, ARegistrationRefusesFourBareStringsPositionally)
{
    using Reg = ReolinkEventRegistry::CameraRegistration;

    EXPECT_FALSE((isBraceInitializable<Reg, string, string, string, string>))
        << "CameraRegistration{h, u, p, e} still compiles - this is the exact "
           "form ReolinkCtrl.cpp:100 writes, and it puts the password wherever "
           "the caller happens to have typed it";

    EXPECT_FALSE((isDirectInitializable<Reg, string, string, string, string>))
        << "CameraRegistration(h, u, p, e) still compiles - a positional "
           "constructor is not a fix, it is the same hole with a name on it";

    //The counterpart: the typed form IS accepted, so the two above are not
    //passing because the type became unbuildable.
    EXPECT_TRUE((isDirectInitializable<Reg, ReolinkTypes::Hostname,
                                       ReolinkTypes::Username,
                                       ReolinkTypes::Password,
                                       ReolinkTypes::EventType>));

    //⭐ And the permutation of the typed form is refused - username and
    //password swapped, which is the exact defect F-REO-5 names.
    EXPECT_FALSE((isDirectInitializable<Reg, ReolinkTypes::Hostname,
                                        ReolinkTypes::Password,
                                        ReolinkTypes::Username,
                                        ReolinkTypes::EventType>));
    EXPECT_FALSE((isBraceInitializable<Reg, ReolinkTypes::Hostname,
                                       ReolinkTypes::Password,
                                       ReolinkTypes::Username,
                                       ReolinkTypes::EventType>));
}

/* T3.31 - the fields still land where their names say, after the shape
 * change. The four fixture values are mutually non-substitutable, so this
 * would fall on any permutation inside the constructor itself - which is the
 * one hop the typing moved the risk INTO. */
TEST(ReolinkRegistry, TheTypedConstructorFillsEachFieldFromItsOwnWrapper)
{
    const ReolinkEventRegistry::CameraRegistration reg(
        ReolinkTypes::Hostname("cam-north.lan"),
        ReolinkTypes::Username("operator-7"),
        ReolinkTypes::Password("Kx9!vQm2#Zt"),
        ReolinkTypes::EventType("person"));

    EXPECT_EQ("cam-north.lan", reg.hostname);
    EXPECT_EQ("operator-7", reg.username);
    EXPECT_EQ("Kx9!vQm2#Zt", reg.password);
    EXPECT_EQ("person", reg.event_type);
}

/* The probes above are only worth their line count if they can tell the two
 * situations apart. A trait that answers false to everything would pass them
 * for free.
 *
 * ⭐⭐ AND THEY HAVE TWO BLIND SPOTS, BOTH IN THE DANGEROUS DIRECTION - the
 * reviewer of this ticket made the probes report CLOSED on a type that is
 * WIDE OPEN, twice. They are written down here because a probe whose blind
 * spots are documented is worth more than one everybody believes is total.
 * Neither of them bites what THIS ticket delivers, and the reason is a
 * property of the delivered code, not luck - see each witness below.
 *
 *   (a) LVALUE REFERENCE. std::declval<std::string>() yields an RVALUE, so a
 *       constructor taking `std::string &` binds nothing and BOTH probes
 *       answer false - they report "closed" while
 *       `std::string h, u, p, e; LieRef bad(h, p, u, e);` compiles happily
 *       and puts the password in the username. The probe as spelled says
 *       nothing about lvalue arguments; you have to ask it with `string &`.
 *       ⇒ Does not bite here: every delivered wrapper takes its payload BY
 *       VALUE (`explicit Hostname(std::string s)`), never by reference.
 *
 *   (b) NARROWING. isBraceInitializable<T, int> is false for a NON-explicit
 *       `T(unsigned short)` for a reason that has nothing to do with the
 *       guard: braces refuse the narrowing int -> unsigned short. Read as
 *       "closed" it hides workaround W1 standing wide open, because
 *       T obj(someInt) still compiles.
 *       ⇒ Does not bite here: the brace probe is only ever pointed at the
 *       std::string wrappers, where no narrowing exists, and the numeric Wago
 *       wrappers are probed with is_invocable_v in WagoWire_test instead. */
TEST(ReolinkRegistry, TheAggregateProbesActuallyDiscriminate)
{
    struct Aggregate { std::string a, b; };
    struct Positional { Positional(std::string, std::string) {} };
    struct Guarded
    {
        struct A { std::string v; explicit A(std::string s): v(std::move(s)) {} };
        struct B { std::string v; explicit B(std::string s): v(std::move(s)) {} };
        Guarded(A, B) {}
    };

    EXPECT_TRUE((isBraceInitializable<Aggregate, string, string>));
    EXPECT_TRUE((isDirectInitializable<Aggregate, string, string>)); //C++20

    //⭐ The measurement that infirms T3.31 acceptance criterion 3: a plain
    //constructor leaves BOTH doors open.
    EXPECT_TRUE((isBraceInitializable<Positional, string, string>));
    EXPECT_TRUE((isDirectInitializable<Positional, string, string>));

    //Per-field strong types, and only they, shut both.
    EXPECT_FALSE((isBraceInitializable<Guarded, string, string>));
    EXPECT_FALSE((isDirectInitializable<Guarded, string, string>));

    /*----------------------------------------------------------------------
     * ⭐ WITNESS (a) - THE LVALUE-REFERENCE BLIND SPOT, MADE VISIBLE.
     *
     * LieRef is a permutable type: two adjacent std::string taken by lvalue
     * reference, exactly the shape this ticket exists to refuse. Asked with
     * plain `string` the probes say FALSE - i.e. "closed" - and they are
     * WRONG. Asked with `string &` they see it.
     *--------------------------------------------------------------------*/
    struct LieRef { LieRef(std::string &, std::string &) {} };

    //What the probes report as spelled elsewhere in this file: closed. False.
    EXPECT_FALSE((isDirectInitializable<LieRef, string, string>));
    EXPECT_FALSE((isBraceInitializable<LieRef, string, string>));

    //What is actually true: two adjacent lvalue strings, in any order.
    EXPECT_TRUE((isDirectInitializable<LieRef, string &, string &>))
        << "if this fails the blind spot has closed on its own and the "
           "comment above must be rewritten";
    EXPECT_TRUE((isBraceInitializable<LieRef, string &, string &>));

    //And the delivered wrappers survive the SAME question - which is the
    //point: they are refused for lvalues too, not merely for rvalues.
    EXPECT_FALSE((isDirectInitializable<Guarded, string &, string &>));
    EXPECT_FALSE((isBraceInitializable<Guarded, string &, string &>));

    //⭐ The property that makes (a) inapplicable to the delivered code, so
    //this is checked and not merely asserted in prose: every wrapper takes
    //its payload BY VALUE.
    EXPECT_TRUE((std::is_constructible_v<ReolinkTypes::Hostname, string &>));
    EXPECT_TRUE((std::is_constructible_v<ReolinkTypes::EventData, const string &>));

    /*----------------------------------------------------------------------
     * ⭐ WITNESS (b) - THE NARROWING BLIND SPOT.
     *
     * Narrowing is WIDE OPEN: a non-explicit constructor, so a bare int walks
     * straight in (workaround W1). The brace probe answers false anyway,
     * because braces refuse int -> unsigned short. Read as "closed" it is a
     * false green.
     *--------------------------------------------------------------------*/
    struct Narrowing   { unsigned short v; Narrowing(unsigned short s): v(s) {} };
    struct NoNarrowing { unsigned short v; explicit NoNarrowing(unsigned short s): v(s) {} };

    //⭐ The brace probe cannot tell these two apart: it answers false for
    //BOTH, and for a reason that is not the guard - braces refuse the
    //narrowing int -> unsigned short whether the constructor is explicit or
    //not. One of the two is wide open and the probe says "closed" to both.
    EXPECT_FALSE((isBraceInitializable<Narrowing, int>));
    EXPECT_FALSE((isBraceInitializable<NoNarrowing, int>));

    //⚠️ And direct-init cannot tell them apart either, in the OTHER
    //direction: an explicit constructor is still directly callable, so both
    //answer true. is_constructible is not a guard check.
    EXPECT_TRUE((isDirectInitializable<Narrowing, int>));
    EXPECT_TRUE((isDirectInitializable<NoNarrowing, int>));

    //⭐ What DOES discriminate, and what a numeric wrapper must therefore be
    //probed with: copy-initialisation. It is the question a call site asks -
    //"may a bare int become one of these on its own?" - and it is the one
    //`explicit` answers. This is why the Wago wrappers are pinned with
    //is_invocable_v/is_convertible_v in WagoWire_test and NOT with the brace
    //probe above.
    EXPECT_TRUE((std::is_convertible_v<int, Narrowing>))
        << "W1 wide open, and the brace probe never saw it";
    EXPECT_FALSE((std::is_convertible_v<int, NoNarrowing>));
}

/*----------------------------------------------------------------------------
 * T3.31 (review) - ⭐ THE EVENT HALF OF THE CHAIN.
 *
 * The first delivery of this ticket typed the REGISTRATION path and left the
 * EVENT path bare, and the reviewer's two mutations found it:
 *
 *   RA   ReolinkCtrl.cpp:108   cameraKey(reg.event_type, reg.hostname)
 *   RB   ReolinkCtrl.cpp:81    dispatch(event_type, hostname, event_data)
 *
 * Both COMPILED - `make -j12`, zero `error:`, zero warning - and both were
 * green by construction, because `nm` finds ZERO ReolinkCtrl symbol in any
 * test binary in the tree. RB is the dangerous one: a swapped dispatch()
 * looks up a key nothing was ever registered under, returns 0, and EVERY
 * EVENT FROM EVERY CAMERA IS DROPPED IN SILENCE - no error, no log, nothing
 * failing anywhere.
 *
 * The cases below are the oracle those two mutations did not have. They are a
 * COMPILATION property reported through an executable assertion: the expected
 * verdict for a typing fix is a refusal to compile, and this is how a refusal
 * to compile gets a test case at all.
 *
 * ⚠️ What they do NOT close, declared and not promised: wrapping the WRONG
 * variable. cameraKey(Hostname(reg.event_type), EventType(reg.hostname))
 * type-checks and always will (workaround W3, INTRINSIC). What is gone is the
 * ORDER, at every hop.
 *--------------------------------------------------------------------------*/

namespace
{

/* Does a call with this argument list type-check? The question a call site
 * asks - copy-initialisation of each parameter - which is exactly the one
 * `explicit` answers, and NOT the one is_constructible answers. */
template <class F, class... A>
constexpr bool isCallable = std::is_invocable_v<F, A...>;

} //namespace

TEST(ReolinkRegistry, TheEventChainRefusesPermutedArguments)
{
    using Reg = ReolinkEventRegistry;

    /* --- RA: cameraKey ------------------------------------------------- */
    auto key2 = [](const RH &h, const RE &e) { return Reg::cameraKey(h, e); };

    EXPECT_TRUE((isCallable<decltype(key2), RH, RE>));
    EXPECT_FALSE((isCallable<decltype(key2), RE, RH>))
        << "cameraKey(event_type, hostname) compiles again - this is mutation "
           "RA, and it builds a key nothing is registered under";
    //And the shape the mutation was actually WRITTEN in: two bare strings.
    EXPECT_FALSE((isCallable<decltype(key2), string, string>));

    /* --- RB: dispatch -------------------------------------------------- */
    ReolinkEventRegistry reg;
    auto disp = [&reg](const RH &h, const RE &e, const RD &d)
                { return reg.dispatch(h, e, d); };

    EXPECT_TRUE((isCallable<decltype(disp), RH, RE, RD>));
    EXPECT_FALSE((isCallable<decltype(disp), RE, RH, RD>))
        << "dispatch(event_type, hostname, event_data) compiles again - this "
           "is mutation RB, and it silently drops every camera event";
    EXPECT_FALSE((isCallable<decltype(disp), RH, RD, RE>));
    EXPECT_FALSE((isCallable<decltype(disp), string, string, string>));

    /* --- the two queries ----------------------------------------------- */
    auto has = [&reg](const RH &h, const RE &e) { return reg.hasCamera(h, e); };
    auto cnt = [&reg](const RH &h, const RE &e) { return reg.callbackCount(h, e); };

    EXPECT_TRUE((isCallable<decltype(has), RH, RE>));
    EXPECT_FALSE((isCallable<decltype(has), RE, RH>));
    EXPECT_FALSE((isCallable<decltype(has), string, string>));
    EXPECT_TRUE((isCallable<decltype(cnt), RH, RE>));
    EXPECT_FALSE((isCallable<decltype(cnt), RE, RH>));
    EXPECT_FALSE((isCallable<decltype(cnt), string, string>));

    /* --- the callback signature itself --------------------------------- */
    using Cb = ReolinkEventRegistry::EventCallback;
    EXPECT_TRUE((isCallable<Cb, RH, RE, RD>));
    EXPECT_FALSE((isCallable<Cb, RE, RH, RD>))
        << "a callback body that reads (event_type, hostname, ...) type-checks "
           "again - the receiving end of the same defect";
    EXPECT_FALSE((isCallable<Cb, string, string, string>));

    /* --- ⭐ the one-argument overload: no order left at all ------------- */
    EXPECT_TRUE((std::is_invocable_v<
                     std::string (*)(const ReolinkEventRegistry::CameraRegistration &),
                     ReolinkEventRegistry::CameraRegistration>));
}

/* The probes above say a permutation does not compile. This one says the
 * non-permuted path still WORKS, end to end and executed - a refusal to
 * compile is worth nothing if the surviving call delivers to the wrong place.
 * The fixture values are mutually non-substitutable on purpose. */
TEST(ReolinkRegistry, TheTypedEventPathStillDeliversEachValueToItsOwnName)
{
    ReolinkEventRegistry reg;
    string gotHost, gotType, gotData;

    reg.add(makeReg("cam-north.lan", "person"),
            [&](const RH &h, const RE &t, const RD &d)
            { gotHost = h.v; gotType = t.v; gotData = d.v; });

    //The one-argument overload and the two-argument one agree.
    const ReolinkEventRegistry::CameraRegistration r = makeReg("cam-north.lan", "person");
    EXPECT_EQ("cam-north.lan_person", ReolinkEventRegistry::cameraKey(r));
    EXPECT_EQ(ReolinkEventRegistry::cameraKey(RH("cam-north.lan"), RE("person")),
              ReolinkEventRegistry::cameraKey(r));

    EXPECT_EQ(1u, reg.dispatch(RH("cam-north.lan"), RE("person"), RD("{\"alarm\":1}")));
    EXPECT_EQ("cam-north.lan", gotHost);
    EXPECT_EQ("person", gotType);
    EXPECT_EQ("{\"alarm\":1}", gotData);

    //⭐ The RB scenario, run instead of compiled: a lookup under the swapped
    //key finds nobody. This is what the mutation caused for EVERY event.
    EXPECT_EQ(0u, reg.dispatch(RH("person"), RE("cam-north.lan"), RD("{\"alarm\":1}")));
    EXPECT_FALSE(reg.hasCamera(RH("person"), RE("cam-north.lan")));
    EXPECT_TRUE(reg.hasCamera(RH("cam-north.lan"), RE("person")));
}
