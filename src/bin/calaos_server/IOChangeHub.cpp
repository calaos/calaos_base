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
#include <algorithm>

#include "IOChangeHub.h"
#include "ListeRoom.h"

using namespace Calaos;

IOChangeHub &IOChangeHub::Instance()
{
    static IOChangeHub *inst = new IOChangeHub;

    return *inst;
}

IOChangeHub::Subscription::Subscription(Subscription &&other) noexcept:
    id(std::move(other.id)),
    slot(std::move(other.slot))
{
    other.slot.reset();
}

IOChangeHub::Subscription &IOChangeHub::Subscription::operator=(Subscription &&other) noexcept
{
    if (this != &other)
    {
        reset();
        id = std::move(other.id);
        slot = std::move(other.slot);
        other.slot.reset();
    }
    return *this;
}

void IOChangeHub::Subscription::reset()
{
    auto s = slot.lock();
    //active <=> still registered under id: unsubscribe() relies on it.
    if (s && s->active)
        IOChangeHub::Instance().unsubscribe(id, s.get());
    slot.reset();
}

bool IOChangeHub::Subscription::active() const
{
    auto s = slot.lock();
    return s && s->active;
}

IOChangeHub::Subscription IOChangeHub::subscribe(const std::string &id, Callback cb)
{
    auto s = std::make_shared<Slot>();
    s->cb = std::move(cb);
    slots[id].push_back(s);

    Subscription sub;
    sub.id = id;
    sub.slot = s;
    return sub;
}

void IOChangeHub::unsubscribe(const std::string &id, const Slot *slot)
{
    auto it = slots.find(id);
    auto &v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [slot](const std::shared_ptr<Slot> &s)
    {
        if (s.get() != slot) return false;
        //A dispatch may hold this slot in its snapshot: tell it to skip.
        s->active = false;
        return true;
    }), v.end());

    if (v.empty())
        slots.erase(it);
}

void IOChangeHub::notify(const std::string &id)
{
    auto it = slots.find(id);
    if (it == slots.end()) return;

    //The snapshot keeps each slot, and the std::function a callback is
    //running from, alive if that callback unsubscribes itself.
    const std::vector<std::shared_ptr<Slot>> snapshot = it->second;

    for (const auto &s: snapshot)
    {
        if (!s->active) continue;

        //Resolved per call: an earlier callback may have deleted the IO.
        IOBase *io = ListeRoom::Instance().get_io(id);
        if (!io) return;

        s->cb(*io);
    }
}

size_t IOChangeHub::subscriberCount(const std::string &id) const
{
    auto it = slots.find(id);
    return it == slots.end() ? 0 : it->second.size();
}
