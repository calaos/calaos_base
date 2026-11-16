/******************************************************************************
 **  Copyright (c) 2026, Calaos. All Rights Reserved.
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
 * Klf200Registry (IO/Klf200/Klf200Registry.h): register/unregister bookkeeping
 * of the node callbacks held by the Klf200Ctrl singleton, the sharing of one
 * node between several IOs (a window's shutter and its rain sensor), and
 * the reentrant-safe dispatch. Pure std, no process, no loop - same shape as
 * tests/ReolinkRegistry_test.cpp.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Params.h"
#include "Klf200Registry.h"

using Klf200Types::Host;
using Klf200Types::NodeId;
using Klf200Types::Password;

namespace
{

Klf200Registry::NodeRegistration reg(const std::string &host, int node, bool rain = false)
{
    return Klf200Registry::NodeRegistration(Host(host), Password("pw"), NodeId(node), rain);
}

Params nodeEvent(const std::string &position)
{
    Params p;
    p.Add("event", "node");
    p.Add("position", position);
    return p;
}

} //namespace

TEST(Klf200Registry, KeyIsHostSlashNode)
{
    EXPECT_EQ("klf/3", Klf200Registry::nodeKey(Host("klf"), NodeId(3)));
    EXPECT_EQ("klf/3", Klf200Registry::nodeKey(reg("klf", 3)));
    //Two gateways may well number their nodes the same way
    EXPECT_NE(Klf200Registry::nodeKey(Host("a"), NodeId(1)), Klf200Registry::nodeKey(Host("b"), NodeId(1)));
}

TEST(Klf200Registry, AddReturnsDistinctNonZeroIds)
{
    Klf200Registry r;
    const auto a = r.add(reg("h", 1), [](const Params &)
                         {
                         });
    const auto b = r.add(reg("h", 1), [](const Params &)
                         {
                         });
    EXPECT_NE(Klf200Registry::INVALID_ID, a);
    EXPECT_NE(Klf200Registry::INVALID_ID, b);
    EXPECT_NE(a, b);
    EXPECT_EQ(1u, r.nodeCount()); //one record for the two callbacks
}

TEST(Klf200Registry, DispatchReachesEveryCallbackOfTheNodeAndNoOther)
{
    Klf200Registry r;
    std::vector<std::string> seen;
    r.add(reg("h", 1), [&](const Params &p)
          {
        seen.push_back("a:" + p["position"]);
    });
    r.add(reg("h", 1), [&](const Params &p)
          {
        seen.push_back("b:" + p["position"]);
    });
    r.add(reg("h", 2), [&](const Params &p)
          {
        seen.push_back("other:" + p["position"]);
    });

    EXPECT_EQ(2u, r.dispatch(Host("h"), NodeId(1), nodeEvent("42")));
    ASSERT_EQ(2u, seen.size());
    EXPECT_EQ("a:42", seen[0]);
    EXPECT_EQ("b:42", seen[1]);

    EXPECT_EQ(0u, r.dispatch(Host("h"), NodeId(9), nodeEvent("1")));
    EXPECT_EQ(0u, r.dispatch(Host("nope"), NodeId(1), nodeEvent("1")));
}

TEST(Klf200Registry, RainFlagAccumulatesAcrossRegistrationsOfOneNode)
{
    //The shutter IO registers without rain, the rain sensor IO of the same
    //window registers with it: the record must end up asking for rain, in
    //whichever order io.xml lists the two.
    Klf200Registry r;
    r.add(reg("h", 1, false), [](const Params &)
          {
          });
    r.add(reg("h", 1, true), [](const Params &)
          {
          });
    bool rain = false;
    r.forEachRegistration([&](const Klf200Registry::NodeRegistration &n)
                          {
        rain = n.rain;
    });
    EXPECT_TRUE(rain);

    Klf200Registry r2;
    r2.add(reg("h", 1, true), [](const Params &)
           {
           });
    r2.add(reg("h", 1, false), [](const Params &)
           {
           });
    rain = false;
    r2.forEachRegistration([&](const Klf200Registry::NodeRegistration &n)
                           {
        rain = n.rain;
    });
    EXPECT_TRUE(rain);
}

TEST(Klf200Registry, FirstPasswordOfANodeIsKept)
{
    Klf200Registry r;
    r.add(Klf200Registry::NodeRegistration(Host("h"), Password("first"), NodeId(1), false), [](const Params &)
          {
          });
    r.add(Klf200Registry::NodeRegistration(Host("h"), Password("second"), NodeId(1), false), [](const Params &)
          {
          });
    std::string pw;
    r.forEachRegistration([&](const Klf200Registry::NodeRegistration &n)
                          {
        pw = n.password;
    });
    EXPECT_EQ("first", pw);
}

TEST(Klf200Registry, ThePasswordConflictIsPerGateway)
{
    Klf200Registry r;
    const auto first = Klf200Registry::NodeRegistration(Host("h"), Password("first"), NodeId(1), false);
    EXPECT_FALSE(r.passwordConflicts(first));
    r.add(first, [](const Params &)
          {
          });

    //Another node of the same gateway, another password: still one gateway
    EXPECT_TRUE(r.passwordConflicts(Klf200Registry::NodeRegistration(Host("h"), Password("second"), NodeId(2), false)));
    EXPECT_FALSE(r.passwordConflicts(Klf200Registry::NodeRegistration(Host("h"), Password("first"), NodeId(2), false)));
    EXPECT_FALSE(r.passwordConflicts(Klf200Registry::NodeRegistration(Host("other"), Password("second"), NodeId(1), false)));
}

TEST(Klf200Registry, RegistrationLookupsReturnTheMergedRecord)
{
    Klf200Registry r;
    const auto a = r.add(reg("h", 1), [](const Params &)
                         {
                         });
    const auto b = r.add(reg("h", 1, true), [](const Params &)
                         {
                         });

    const Klf200Registry::NodeRegistration *byNode = r.registration(Host("h"), NodeId(1));
    ASSERT_NE(nullptr, byNode);
    EXPECT_TRUE(byNode->rain);
    EXPECT_EQ(byNode, r.registrationOf(a));
    EXPECT_EQ(byNode, r.registrationOf(b));
    EXPECT_EQ(nullptr, r.registration(Host("h"), NodeId(2)));
    EXPECT_EQ(nullptr, r.registrationOf(Klf200Registry::INVALID_ID));

    r.remove(a);
    r.remove(b);
    EXPECT_EQ(nullptr, r.registrationOf(a));
    EXPECT_EQ(nullptr, r.registration(Host("h"), NodeId(1)));
}

TEST(Klf200Registry, RemoveLastCallbackDropsTheRecord)
{
    Klf200Registry r;
    const auto a = r.add(reg("h", 1), [](const Params &)
                         {
                         });
    const auto b = r.add(reg("h", 1), [](const Params &)
                         {
                         });

    std::string key;
    EXPECT_FALSE(r.remove(a, &key)); //b still there
    EXPECT_TRUE(r.hasNode(Host("h"), NodeId(1)));
    EXPECT_TRUE(r.remove(b, &key));
    EXPECT_EQ("h/1", key);
    EXPECT_FALSE(r.hasNode(Host("h"), NodeId(1)));
    EXPECT_TRUE(r.empty());

    //Unknown ids are ignored
    EXPECT_FALSE(r.remove(a));
    EXPECT_FALSE(r.remove(Klf200Registry::INVALID_ID));
    EXPECT_FALSE(r.remove(12345));
}

TEST(Klf200Registry, RemovedCallbackIsNeverInvokedAgain)
{
    Klf200Registry r;
    int calls = 0;
    const auto id = r.add(reg("h", 1), [&](const Params &)
                          {
        calls++;
    });
    r.dispatch(Host("h"), NodeId(1), nodeEvent("1"));
    r.remove(id);
    r.dispatch(Host("h"), NodeId(1), nodeEvent("2"));
    EXPECT_EQ(1, calls);
}

TEST(Klf200Registry, ACallbackThatUnregistersItsNeighbourDoesNotGetItInvoked)
{
    //Reentrancy: the first callback of the dispatch deletes the IO owning
    //the second one (config reload from inside an event). The second must
    //not fire on a dead object.
    Klf200Registry r;
    Klf200Registry::RegistrationId second = Klf200Registry::INVALID_ID;
    int secondCalls = 0;
    r.add(reg("h", 1), [&](const Params &)
          {
        r.remove(second);
    });
    second = r.add(reg("h", 1), [&](const Params &)
                   {
        secondCalls++;
    });

    EXPECT_EQ(1u, r.dispatch(Host("h"), NodeId(1), nodeEvent("1")));
    EXPECT_EQ(0, secondCalls);
}

TEST(Klf200Registry, ACallbackMayRegisterDuringDispatch)
{
    Klf200Registry r;
    int lateCalls = 0;
    r.add(reg("h", 1), [&](const Params &)
          {
        r.add(reg("h", 1), [&](const Params &)
              {
            lateCalls++;
        });
    });

    //The newcomer is not part of the running dispatch (copy), but is there
    //for the next one.
    EXPECT_EQ(1u, r.dispatch(Host("h"), NodeId(1), nodeEvent("1")));
    EXPECT_EQ(0, lateCalls);
    EXPECT_EQ(2u, r.dispatch(Host("h"), NodeId(1), nodeEvent("2")));
    EXPECT_EQ(1, lateCalls);
}

TEST(Klf200Registry, HasHostOnlyKnowsRegisteredGateways)
{
    Klf200Registry r;
    EXPECT_FALSE(r.hasHost("h"));
    const auto id = r.add(reg("h", 1), [](const Params &)
                          {
                          });
    EXPECT_TRUE(r.hasHost("h"));
    EXPECT_FALSE(r.hasHost("other"));
    r.remove(id);
    EXPECT_FALSE(r.hasHost("h"));
}

//Klf200GatewayLinks: what Klf200Ctrl asks before sending a command, so that
//an IO refuses an order the gateway cannot receive instead of reporting it.

TEST(Klf200GatewayLinks, NothingCanBeSentBeforeTheProcessConnects)
{
    Klf200GatewayLinks links;
    EXPECT_FALSE(links.processUp());
    links.gatewayStatus("klf", true);
    EXPECT_FALSE(links.canSend("klf"));
}

TEST(Klf200GatewayLinks, ACommandNeedsTheProcessAndThatGatewaySession)
{
    Klf200GatewayLinks links;
    links.processConnected();
    EXPECT_TRUE(links.processUp());
    EXPECT_FALSE(links.canSend("klf")); //process up, session not reported yet
    links.gatewayStatus("klf", true);
    EXPECT_TRUE(links.canSend("klf"));
    EXPECT_FALSE(links.canSend("other")); //per gateway, not global
    links.gatewayStatus("klf", false);
    EXPECT_FALSE(links.canSend("klf"));
}

TEST(Klf200GatewayLinks, AProcessRestartForgetsEverySession)
{
    Klf200GatewayLinks links;
    links.processConnected();
    links.gatewayStatus("klf", true);
    links.processExited();
    EXPECT_FALSE(links.canSend("klf"));
    //The new process reports its own sessions: none is assumed up
    links.processConnected();
    EXPECT_FALSE(links.canSend("klf"));
}
