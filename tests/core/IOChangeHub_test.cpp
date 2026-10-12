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

//IOChangeHub: synchronous per-IO change subscription. The lifetime cases are
//only meaningful under the ASan build (--enable-asan), where a callback left
//behind by a dead subscriber is a heap-use-after-free.

#include "CalaosCoreFixture.h"
#include "IOChangeHub.h"

#include <memory>
#include <string>
#include <vector>

using namespace Calaos;
using namespace CalaosTest;

namespace
{

const char *const ID_B = "hub_b";
const char *const ID_C = "hub_c";

IOChangeHub &hub() { return IOChangeHub::Instance(); }

} //namespace

class IOChangeHubTest: public CoreFixture
{
protected:
    void SetUp() override
    {
        CoreFixture::SetUp();
        loadConfig(minimalIoXml(), rulesXmlDocument(""));
        ASSERT_NE(createInternalIO("InternalBool", ID_B, "B"), nullptr);
        ASSERT_NE(createInternalIO("InternalBool", ID_C, "C"), nullptr);
    }

    void TearDown() override
    {
        //Every subscription of a test is scoped to its body.
        EXPECT_EQ(hub().idCount(), 0u);
        CoreFixture::TearDown();
    }
};

TEST_F(IOChangeHubTest, SubscriberReceivesEveryEmissionWithTheIo)
{
    std::vector<std::string> seen;
    auto sub = hub().subscribe(ID_BOOL_IN, [&seen](IOBase &io)
                               {
        seen.push_back(io.get_param("id") + "=" + (io.get_value_bool() ? "1" : "0"));
    });

    io(ID_BOOL_IN)->set_value(true);
    io(ID_BOOL_IN)->set_value(false);
    io(ID_BOOL_OUT)->set_value(true);

    EXPECT_EQ(seen, (std::vector<std::string>{ std::string(ID_BOOL_IN) + "=1",
                                               std::string(ID_BOOL_IN) + "=0" }));
}

TEST_F(IOChangeHubTest, ResetUnsubscribesAndLeavesNoEntry)
{
    int hits = 0;
    auto sub = hub().subscribe(ID_BOOL_IN, [&hits](IOBase &)
                               {
        hits++;
    });
    EXPECT_TRUE(sub.active());
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 1u);
    EXPECT_EQ(hub().idCount(), 1u);

    io(ID_BOOL_IN)->set_value(true);
    sub.reset();
    io(ID_BOOL_IN)->set_value(false);

    EXPECT_EQ(hits, 1);
    EXPECT_FALSE(sub.active());
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 0u);
    //An id without subscribers costs one lookup and nothing more.
    EXPECT_EQ(hub().idCount(), 0u);

    sub.reset();
    IOChangeHub::Subscription empty;
    EXPECT_FALSE(empty.active());
}

TEST_F(IOChangeHubTest, MultipleSubscribersRunInSubscriptionOrder)
{
    std::vector<int> order;
    auto s1 = hub().subscribe(ID_BOOL_IN, [&order](IOBase &)
                              {
        order.push_back(1);
    });
    auto s2 = hub().subscribe(ID_BOOL_IN, [&order](IOBase &)
                              {
        order.push_back(2);
    });
    auto s3 = hub().subscribe(ID_BOOL_IN, [&order](IOBase &)
                              {
        order.push_back(3);
    });
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 3u);

    io(ID_BOOL_IN)->set_value(true);
    s2.reset();
    io(ID_BOOL_IN)->set_value(false);

    EXPECT_EQ(order, (std::vector<int>{ 1, 2, 3, 1, 3 }));
}

TEST_F(IOChangeHubTest, MovedSubscriptionKeepsTheCallback)
{
    int hits = 0;
    auto a = hub().subscribe(ID_BOOL_IN, [&hits](IOBase &)
                             {
        hits++;
    });
    IOChangeHub::Subscription b(std::move(a));
    EXPECT_FALSE(a.active());
    EXPECT_TRUE(b.active());

    IOChangeHub::Subscription c;
    c = std::move(b);
    EXPECT_FALSE(b.active());
    io(ID_BOOL_IN)->set_value(true);
    EXPECT_EQ(hits, 1);

    //Assigning over a live subscription releases the old one.
    c = hub().subscribe(ID_B, [](IOBase &)
                        {
                        });
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 0u);
    EXPECT_EQ(hub().subscriberCount(ID_B), 1u);
    io(ID_BOOL_IN)->set_value(false);
    EXPECT_EQ(hits, 1);
}

TEST_F(IOChangeHubTest, SubscriptionTakenBeforeTheIoExists)
{
    int hits = 0;
    auto sub = hub().subscribe("hub_late", [&hits](IOBase &)
                               {
        hits++;
    });

    hub().notify("hub_late");
    EXPECT_EQ(hits, 0);

    IOBase *late = createInternalIO("InternalBool", "hub_late", "Late");
    ASSERT_NE(late, nullptr);
    late->set_value(true);
    EXPECT_EQ(hits, 1);
}

TEST_F(IOChangeHubTest, SubscriberDestroyedBeforeTheIo)
{
    struct Watcher
    {
        int hits = 0;
        IOChangeHub::Subscription sub;
        explicit Watcher(const std::string &id)
        {
            sub = hub().subscribe(id, [this](IOBase &)
                                  {
                hits++;
            });
        }
    };

    auto w = std::make_unique<Watcher>(ID_BOOL_IN);
    io(ID_BOOL_IN)->set_value(true);
    EXPECT_EQ(w->hits, 1);

    w.reset();
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 0u);
    //Would touch the freed Watcher if the callback had survived it.
    io(ID_BOOL_IN)->set_value(false);
}

TEST_F(IOChangeHubTest, IoDeletedBeforeTheSubscriber)
{
    int hits = 0;
    auto sub = hub().subscribe(ID_BOOL_IN, [&hits](IOBase &)
                               {
        hits++;
    });

    ASSERT_TRUE(deleteIO(io(ID_BOOL_IN)));
    EXPECT_TRUE(sub.active());
    hub().notify(ID_BOOL_IN);
    EXPECT_EQ(hits, 0);

    //The subscription is to the id: a new IO under it is followed.
    IOBase *again = createInternalIO("InternalBool", ID_BOOL_IN, "Again");
    ASSERT_NE(again, nullptr);
    again->set_value(true);
    EXPECT_EQ(hits, 1);

    ASSERT_TRUE(deleteIO(again));
    sub.reset();
}

//A callback runs under the rule lock, and the IO that started the march still
//has its set_value() on the stack: deleteIO() must refuse.
TEST_F(IOChangeHubTest, DeleteIoIsRefusedDuringDispatch)
{
    ASSERT_NE(addSimpleRule("a_to_b", ID_BOOL_IN, "==", "true", ID_B, "true"), nullptr);

    bool deleted = true;
    int laterHits = 0;
    auto deleter = hub().subscribe(ID_B, [&deleted](IOBase &io)
                                   {
        deleted = deleteIO(&io);
    });
    auto later = hub().subscribe(ID_B, [&laterHits](IOBase &)
                                 {
        laterHits++;
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_FALSE(deleted);
    EXPECT_EQ(laterHits, 1);
    ASSERT_NE(io(ID_B), nullptr);
    EXPECT_FALSE(ListeRule::Instance().isExecutionLocked());
    EXPECT_TRUE(deleteIO(io(ID_B)));
}

//The IO that started the cascade: freed, Internal::set_value() would go on
//with Save() on it after EmitSignalIO() returns.
TEST_F(IOChangeHubTest, DeletingTheCascadeOriginFromACallbackIsRefused)
{
    ASSERT_NE(addSimpleRule("a_to_b", ID_BOOL_IN, "==", "true", ID_B, "true"), nullptr);

    bool deleted = true;
    auto sub = hub().subscribe(ID_B, [&deleted](IOBase &)
                               {
        deleted = deleteIO(io(ID_BOOL_IN));
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_FALSE(deleted);
    ASSERT_NE(io(ID_BOOL_IN), nullptr);
    EXPECT_TRUE(io(ID_BOOL_IN)->get_value_bool());
}

//delete_io() is not guarded: the next subscriber must not get the freed IO.
//B's trigger is queued by the rule, so B itself is not on the stack.
TEST_F(IOChangeHubTest, IoRemovedThroughDeleteIoBypassSkipsLaterSubscribers)
{
    ASSERT_NE(addSimpleRule("a_to_b", ID_BOOL_IN, "==", "true", ID_B, "true"), nullptr);

    int laterHits = 0;
    auto remover = hub().subscribe(ID_B, [](IOBase &io)
                                   {
        EXPECT_TRUE(ListeRoom::Instance().delete_io(&io));
    });
    auto later = hub().subscribe(ID_B, [&laterHits](IOBase &)
                                 {
        laterHits++;
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_EQ(laterHits, 0);
    EXPECT_EQ(io(ID_B), nullptr);
}

TEST_F(IOChangeHubTest, CallbackReadsTheCurrentStateNotTheQueuedWrite)
{
    std::vector<bool> seenByB;
    auto sa = hub().subscribe(ID_BOOL_IN, [](IOBase &)
                              {
        io(ID_B)->set_value(true);
        io(ID_B)->set_value(false);
    });
    auto sb = hub().subscribe(ID_B, [&seenByB](IOBase &io)
                              {
        seenByB.push_back(io.get_value_bool());
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_EQ(seenByB, (std::vector<bool>{ false, false }));
}

TEST_F(IOChangeHubTest, CallbackWritingAnotherIoIsQueuedNotRecursive)
{
    //A -> rule -> OUT ; A -> callback -> B ; B -> rule -> C
    ASSERT_NE(addSimpleRule("a_to_out", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);
    ASSERT_NE(addSimpleRule("b_to_c", ID_B, "==", "true", ID_C, "true"), nullptr);

    std::vector<std::string> log;
    int depth = 0, maxDepth = 0;
    auto record = [&](const std::string &name)
    {
        return [&, name](IOBase &)
        {
            depth++;
            maxDepth = std::max(maxDepth, depth);
            log.push_back(name);
            if (name == "A")
                io(ID_B)->set_value(true);
            depth--;
            };
    };

    auto sa = hub().subscribe(ID_BOOL_IN, record("A"));
    auto sb = hub().subscribe(ID_B, record("B"));
    auto sc = hub().subscribe(ID_C, record("C"));
    auto so = hub().subscribe(ID_BOOL_OUT, record("OUT"));

    io(ID_BOOL_IN)->set_value(true);

    //FIFO of the rule march: OUT was queued by the rule before B by the callback.
    EXPECT_EQ(log, (std::vector<std::string>{ "A", "OUT", "B", "C" }));
    EXPECT_EQ(maxDepth, 1);
    EXPECT_TRUE(io(ID_C)->get_value_bool());
}

TEST_F(IOChangeHubTest, RulesOfATriggerRunBeforeItsCallbacks)
{
    ASSERT_NE(addSimpleRule("a_to_out", ID_BOOL_IN, "==", "true", ID_BOOL_OUT, "true"), nullptr);
    ASSERT_FALSE(io(ID_BOOL_OUT)->get_value_bool());

    std::vector<std::string> log;
    bool outSeenByA = false;
    auto sa = hub().subscribe(ID_BOOL_IN, [&](IOBase &)
                              {
        log.push_back("A");
        outSeenByA = io(ID_BOOL_OUT)->get_value_bool();
    });
    auto so = hub().subscribe(ID_BOOL_OUT, [&](IOBase &)
                              {
        log.push_back("OUT");
    });

    io(ID_BOOL_IN)->set_value(true);

    EXPECT_TRUE(outSeenByA);
    EXPECT_EQ(log, (std::vector<std::string>{ "A", "OUT" }));
}

TEST_F(IOChangeHubTest, SubscribeAndUnsubscribeDuringDispatch)
{
    std::vector<std::string> log;
    IOChangeHub::Subscription s1, s2, s3, s4;

    s1 = hub().subscribe(ID_BOOL_IN, [&](IOBase &)
                         {
        log.push_back("1");
        s2.reset();
        s1.reset(); //its own std::function is running
    });
    s2 = hub().subscribe(ID_BOOL_IN, [&](IOBase &)
                         {
        log.push_back("2");
    });
    s3 = hub().subscribe(ID_BOOL_IN, [&](IOBase &)
                         {
        log.push_back("3");
        if (!s4.active())
            s4 = hub().subscribe(ID_BOOL_IN, [&](IOBase &)
                                 {
                log.push_back("4");
            });
    });

    io(ID_BOOL_IN)->set_value(true);
    EXPECT_EQ(log, (std::vector<std::string>{ "1", "3" }));

    log.clear();
    io(ID_BOOL_IN)->set_value(false);
    EXPECT_EQ(log, (std::vector<std::string>{ "3", "4" }));
    EXPECT_EQ(hub().subscriberCount(ID_BOOL_IN), 2u);

    s3.reset();
    s4.reset();
}
