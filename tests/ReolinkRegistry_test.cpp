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

static ReolinkEventRegistry::CameraRegistration makeReg(const string &host, const string &evt)
{
    return {host, "user", "secret", evt};
}

TEST(ReolinkRegistry, CameraKeyFormat)
{
    EXPECT_EQ("10.0.0.5_motion", ReolinkEventRegistry::cameraKey("10.0.0.5", "motion"));
}

TEST(ReolinkRegistry, AddAndDispatchDeliversEvent)
{
    ReolinkEventRegistry reg;
    string gotHost, gotType, gotData;

    auto id = reg.add(makeReg("cam1", "motion"),
                      [&](const string &h, const string &t, const string &d)
                      { gotHost = h; gotType = t; gotData = d; });
    ASSERT_NE(ReolinkEventRegistry::INVALID_ID, id);

    EXPECT_EQ(1u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ("cam1", gotHost);
    EXPECT_EQ("motion", gotType);
    EXPECT_EQ("ON", gotData);

    // Other hostname/event_type must not match
    EXPECT_EQ(0u, reg.dispatch("cam1", "person", "ON"));
    EXPECT_EQ(0u, reg.dispatch("cam2", "motion", "ON"));
}

// The UAF regression scenario: once an IO unregisters (destructor), its
// callback must never be invoked again.
TEST(ReolinkRegistry, RemovedCallbackNeverFiresAgain)
{
    ReolinkEventRegistry reg;
    int fired = 0;

    auto id = reg.add(makeReg("cam1", "motion"),
                      [&](const string &, const string &, const string &) { fired++; });

    EXPECT_EQ(1u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ(1, fired);

    EXPECT_TRUE(reg.remove(id)); //last callback: registration fully dropped
    EXPECT_FALSE(reg.hasCamera("cam1", "motion"));
    EXPECT_TRUE(reg.empty());

    EXPECT_EQ(0u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ(1, fired);
}

TEST(ReolinkRegistry, TwoCallbacksSameCameraRemoveOneKeepsRegistration)
{
    ReolinkEventRegistry reg;
    int firedA = 0, firedB = 0;

    auto idA = reg.add(makeReg("cam1", "motion"),
                       [&](const string &, const string &, const string &) { firedA++; });
    auto idB = reg.add(makeReg("cam1", "motion"),
                       [&](const string &, const string &, const string &) { firedB++; });
    EXPECT_NE(idA, idB);
    EXPECT_EQ(2u, reg.callbackCount("cam1", "motion"));

    EXPECT_EQ(2u, reg.dispatch("cam1", "motion", "ON"));

    // Removing A is not the last one: the camera registration survives
    EXPECT_FALSE(reg.remove(idA));
    EXPECT_TRUE(reg.hasCamera("cam1", "motion"));
    EXPECT_EQ(1u, reg.callbackCount("cam1", "motion"));

    EXPECT_EQ(1u, reg.dispatch("cam1", "motion", "ON"));
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
                      [](const string &, const string &, const string &) {});
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
            [&](const string &, const string &, const string &)
            { reg.remove(idB); }); //first callback kills the second
    idB = reg.add(makeReg("cam1", "motion"),
                  [&](const string &, const string &, const string &) { firedB++; });

    EXPECT_EQ(1u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ(0, firedB); //B was unregistered mid-dispatch: never invoked
}

// A callback that registers a new camera mid-dispatch (config reload adds
// IOs) must not corrupt the iteration nor fire in the same dispatch.
TEST(ReolinkRegistry, ReentrantAddDuringDispatchIsSafe)
{
    ReolinkEventRegistry reg;
    int firedNew = 0;

    reg.add(makeReg("cam1", "motion"),
            [&](const string &, const string &, const string &)
            {
                reg.add(makeReg("cam1", "motion"),
                        [&](const string &, const string &, const string &) { firedNew++; });
            });

    EXPECT_EQ(1u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ(0, firedNew); //not part of the dispatched snapshot

    //second dispatch snapshots [original, first-added]; the original adds
    //yet another callback which again only fires next time
    EXPECT_EQ(2u, reg.dispatch("cam1", "motion", "ON"));
    EXPECT_EQ(1, firedNew);
}

TEST(ReolinkRegistry, ForEachRegistrationListsCrashRecoveryRecords)
{
    ReolinkEventRegistry reg;
    auto noop = [](const string &, const string &, const string &) {};

    reg.add(makeReg("cam1", "motion"), noop);
    reg.add(makeReg("cam1", "motion"), noop); //same camera: one record
    auto id3 = reg.add(makeReg("cam2", "person"), noop);
    EXPECT_EQ(2u, reg.cameraCount());

    std::vector<string> seen;
    reg.forEachRegistration([&](const ReolinkEventRegistry::CameraRegistration &r)
    {
        seen.push_back(ReolinkEventRegistry::cameraKey(r.hostname, r.event_type));
    });
    ASSERT_EQ(2u, seen.size());

    EXPECT_TRUE(reg.remove(id3));
    EXPECT_EQ(1u, reg.cameraCount());
    EXPECT_TRUE(reg.hasCamera("cam1", "motion"));
    EXPECT_FALSE(reg.hasCamera("cam2", "person"));
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
}

/* The probes above are only worth their line count if they can tell the two
 * situations apart. A trait that answers false to everything would pass them
 * for free. */
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
}
