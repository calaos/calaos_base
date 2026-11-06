/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
 ******************************************************************************/

#include <gtest/gtest.h>

#include <memory>

#include "ScopedConnection.h"

TEST(ScopedConnectionTest, ADestroyedHolderCutsItsSlot)
{
    sigc::signal<void> sig;
    int calls = 0;

    {
        ScopedConnection c;
        EXPECT_FALSE(c.connected());

        c = sig.connect([&calls]()
                        {
            calls++;
        });
        EXPECT_TRUE(c.connected());
        sig.emit();
        EXPECT_EQ(calls, 1);
    }

    EXPECT_EQ(sig.size(), 0u);
    sig.emit();
    EXPECT_EQ(calls, 1);
}

TEST(ScopedConnectionTest, ReassigningCutsThePreviousSlot)
{
    sigc::signal<void> sig;
    int first = 0, second = 0;

    ScopedConnection c;
    c = sig.connect([&first]()
                    {
        first++;
    });
    c = sig.connect([&second]()
                    {
        second++;
    });

    EXPECT_EQ(sig.size(), 1u);
    sig.emit();
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

//A controller can die before the IO that listens to it (static teardown).
TEST(ScopedConnectionTest, AHolderOutlivingItsSignalIsHarmless)
{
    auto sig = std::make_unique<sigc::signal<void>>();
    auto c = std::make_unique<ScopedConnection>();
    *c = sig->connect([]()
                      {
                      });
    ASSERT_TRUE(c->connected());

    sig.reset();
    EXPECT_FALSE(c->connected());

    *c = sigc::connection();
    c.reset();
}
