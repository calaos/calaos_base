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
#ifndef S_IOCHANGEHUB_H
#define S_IOCHANGEHUB_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Calaos
{

class IOBase;

/* Synchronous per-IO change subscription, keyed by IO id.
 *
 * A subscription targets an id, not an object: it can be taken before the IO
 * exists (devices resolve their links after every room is loaded), it
 * survives the IO being deleted, and it follows a new IO created under the
 * same id. A renamed IO is no longer seen under its old id.
 *
 * Callbacks run from ListeRule's trigger march (see ListeRule::executeTrigger),
 * so an IO written from a callback is queued like a rule action's write,
 * never handled recursively. A callback therefore reads the IO's CURRENT
 * state, not the value of the write that queued it: two queued writes give
 * two calls that both see the last value.
 *
 * ListeRoom::deleteIO() is refused while the march runs: the IO that started
 * it still has its set_value() on the stack.
 */
class IOChangeHub
{
private:
    struct Slot
    {
        std::function<void(IOBase &)> cb;
        bool active = true;
    };

public:
    using Callback = std::function<void(IOBase &io)>;

    //Move-only handle: the callback is unregistered when it dies.
    class Subscription
    {
    public:
        Subscription();
        ~Subscription();

        Subscription(Subscription &&other) noexcept;
        Subscription &operator=(Subscription &&other) noexcept;
        Subscription(const Subscription &) = delete;
        Subscription &operator=(const Subscription &) = delete;

        void reset();
        bool active() const;

    private:
        friend class IOChangeHub;
        std::string id;
        std::weak_ptr<Slot> slot;
    };

    //Never destroyed: a Subscription held by an object torn down during
    //static destruction (rooms, rules) must still find the hub alive.
    static IOChangeHub &Instance();

    [[nodiscard]] Subscription subscribe(const std::string &id, Callback cb);

    /* Calls every callback registered for `id` with the IO currently known
     * under that id. A callback added during the call waits for the next
     * change; a callback removed during the call is not called any more. */
    void notify(const std::string &id);

    size_t subscriberCount(const std::string &id) const;
    //Number of ids holding at least one subscriber.
    size_t idCount() const { return slots.size(); }

private:
    IOChangeHub() = default;

    void unsubscribe(const std::string &id, const Slot *slot);

    std::unordered_map<std::string, std::vector<std::shared_ptr<Slot>>> slots;
};

} //namespace Calaos

#endif
