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
#ifndef CALAOS_TIMER_H
#define CALAOS_TIMER_H

#include "Utils.h"
#include <functional>
#include <memory>
#include <utility>
#include <sigc++/sigc++.h>

using namespace Utils;

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class TimerHandle;
class IdleHandle;
}

class Timer
{
private:
    std::shared_ptr<uvw::TimerHandle> handleTimer;

    sigc::signal<void, void *> event_signal_data;
    sigc::signal<void> event_signal;
    sigc::connection connection_data;
    sigc::connection connection;

    uint64_t time;
    bool timer_data;

    void *data = nullptr;

    //Lifetime tag: the uvw callback only holds a weak_ptr to it, so a
    //Timer deleted while an event is pending (or from inside its own
    //callback) is never dereferenced afterwards.
    std::shared_ptr<bool> aliveTag;

    void create();

public:
    Timer(double time, sigc::slot<void, void *> slot, void *data);
    Timer(double time, sigc::slot<void> slot);
    ~Timer();

    static void singleShot(double time, sigc::slot<void> slot);

    void Reset();
    void Reset(double time);

    void Tick();

    double getTime() { return time / 1000.0; }
};

class Idler
{
private:
    std::shared_ptr<uvw::IdleHandle> handleIdler;

    //Same lifetime guard as Timer::aliveTag
    std::shared_ptr<bool> aliveTag;

    void createIdler();
public:
    Idler(sigc::slot<void> slot);
    Idler();
    ~Idler();

    static void singleIdler(sigc::slot<void> slot);

    sigc::signal<void> idlerCallback;

};

/* T3.40 - lifetime token for the fire-and-forget one-shots above.
 *
 * Timer::singleShot()/Idler::singleIdler() copy the slot into an ANONYMOUS uvw
 * handle (Timer.cpp:103-123 and :164-176): nothing holds that handle
 * afterwards, so no destructor and no member can ever cancel it. A slot that
 * holds `this` therefore keeps running after its object is gone, and
 * sigc::trackable is no help: it disconnects a sigc::mem_fun, never a lambda
 * capture, which sigc++ only ever sees as an opaque functor.
 *
 * Hold one of these as a member and arm through it rather than through
 * Timer::singleShot() directly:
 *
 *      class Foo { LifetimeTag alive; ... };
 *      alive.singleShot(0.250, [=]() { value = 0; });
 *
 * The witness dies with the object, the token the callback holds expires, and
 * the callback becomes a no-op. Same scheme as Timer/Idler's own aliveTag.
 *
 * ⚠️ The token reaches the callback as a std::weak_ptr, and nothing here hands
 * out the shared_ptr. That is deliberate: a witness captured STRONGLY is kept
 * alive by the pending callback itself, so the guard never bites and the
 * use-after-free quietly becomes a leaked libuv handle instead. Wrapping the
 * capture here is what makes that mistake unwritable at a call site.
 *
 * ⛔ WHAT THIS CLASS DOES NOT DO, and where the answer is instead.
 * It makes a BADLY PLACED guard unwritable. It does nothing about a guard that
 * is simply ABSENT: Timer::singleShot() and Idler::singleIdler() above are
 * still public, and a new call site written with a raw `this` compiles and
 * runs exactly like the twenty T3.40 weighed.
 *
 * Marking them [[deprecated]] was MEASURED, not argued: a full rebuild of the
 * tree with the attribute on both entry points emits 85 warnings over 27
 * distinct call sites - the 4 T3.40 deliberately left raw, the 8 already
 * carrying a guard of their own, the 6 sigc::mem_fun of Squeezebox, the 2
 * that do not capture `this`, and the 2 inside this very class. Every one of
 * them is deliberate; not one is a defect. Rejected.
 *
 * The absent guard is caught one level up instead, by a census of the raw call
 * sites of src/: tests/core/IoLifetimeTimer_test.cpp,
 * IoLifetimeSourceGuardTest. A new fire-and-forget one-shot fails that case
 * until somebody weighs it.
 */
class LifetimeTag
{
public:
    LifetimeTag() = default;

    /* A copy is a NEW owner and gets its own witness: sharing one would make
     * the guard answer for some other object's lifetime. */
    LifetimeTag(const LifetimeTag &) {}
    LifetimeTag &operator=(const LifetimeTag &) { return *this; }

    template <typename F>
    void singleShot(double time, F &&cb) const
    {
        Timer::singleShot(time, guard(std::forward<F>(cb)));
    }

    template <typename F>
    void singleIdler(F &&cb) const
    {
        Idler::singleIdler(guard(std::forward<F>(cb)));
    }

    /* For the rare call site that has to hand the slot to something else. */
    template <typename F>
    auto guard(F &&cb) const
    {
        return [token = std::weak_ptr<bool>(alive), cb = std::forward<F>(cb)]()
        {
            if (token.expired()) return;
            cb();
        };
    }

private:
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};


#endif
