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
#ifndef __REOLINK_EVENT_REGISTRY_H__
#define __REOLINK_EVENT_REGISTRY_H__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ReolinkTypes.h"

/*
 * Bookkeeping for Reolink camera event callbacks, kept free of any process
 * or event-loop dependency so it can be unit tested (see
 * tests/ReolinkRegistry_test.cpp).
 *
 * Each add() returns a RegistrationId that the owning IO must pass back to
 * remove() from its destructor: callbacks live in the long-lived
 * ReolinkCtrl singleton, so an un-removed callback would dangle after the
 * IO is destroyed (config reload) and fire on freed memory.
 */
class ReolinkEventRegistry
{
public:
    using RegistrationId = uint64_t;
    static constexpr RegistrationId INVALID_ID = 0;

    /*
     * T3.31 - three DISTINCT types, never three bare strings.
     *
     * ⚠️ This half of the chain was left untyped by the first delivery of
     * T3.31 and the reviewer's mutation found it: with three std::string in a
     * row, a callback body that reads (event_type, hostname, event_data) - or
     * a dispatch() call that hands them over in that order - compiled with
     * zero diagnostics and lost every event in silence. See ReolinkTypes.h,
     * chain (2).
     */
    using EventCallback = std::function<void(const ReolinkTypes::Hostname &hostname,
                                             const ReolinkTypes::EventType &event_type,
                                             const ReolinkTypes::EventData &event_data)>;

    /*
     * T3.31 - four std::string, and NOT an aggregate.
     *
     * ⚠️ The struct alone closed nothing and this is the measurement that
     * says so: it has carried these four fields by name since E4.1i, and
     * ReolinkCtrl.cpp:100 still built it POSITIONALLY,
     * registry.add({hostname, username, password, event_type}, ...) - so
     * username and password stayed two adjacent strings a caller could swap
     * in silence. A named struct bought a name, not a check.
     *
     * ⭐ MEASURED, and it contradicts what T3.31 asked for. The ticket said
     * to close this "by giving CameraRegistration a constructor". A
     * constructor does remove aggregate-ness, but the constructor is ITSELF
     * positional: with `Reg(string h, string u, string p, string e)` the call
     * `add({h, p, u, e})` still compiles, with zero warnings at -Wall
     * -Wextra -Wconversion. Only per-field strong types close it, so that is
     * what this takes. tests/ReolinkRegistry_test.cpp carries both halves of
     * that measurement.
     *
     * ⚠️ Having a user-provided constructor makes this NOT default
     * constructible, which is deliberate: a half-filled registration has no
     * meaning. add() below therefore uses insert_or_assign() and not
     * operator[], which would require a default constructor.
     */
    struct CameraRegistration
    {
        CameraRegistration(ReolinkTypes::Hostname h,
                           ReolinkTypes::Username u,
                           ReolinkTypes::Password p,
                           ReolinkTypes::EventType e):
            hostname(std::move(h.v)),
            username(std::move(u.v)),
            password(std::move(p.v)),
            event_type(std::move(e.v))
        {}

        std::string hostname;
        std::string username;
        std::string password;
        std::string event_type;
    };

    /*
     * T3.31 - hostname and event_type used to be two bare adjacent
     * std::string here, and cameraKey(event_type, hostname) compiled with no
     * warning at -Wall -Wextra -Wconversion: it builds a key nothing is
     * registered under, so hasCamera() says no and dispatch() delivers to
     * nobody. Measured on the first delivery of this ticket, rc=0.
     */
    static std::string cameraKey(const ReolinkTypes::Hostname &hostname,
                                 const ReolinkTypes::EventType &event_type)
    {
        return hostname.v + "_" + event_type.v;
    }

    /*
     * ⭐ The overload every internal caller uses. ONE argument: there is no
     * order to get wrong, and - unlike the two-argument form - nothing to
     * wrap, so residual W3 (naming the wrong field at the wrapping site) does
     * not apply to it either. The single wrapping site of the whole class is
     * the body below, on the two lines that name the two fields.
     */
    static std::string cameraKey(const CameraRegistration &reg)
    {
        return cameraKey(ReolinkTypes::Hostname(reg.hostname),
                         ReolinkTypes::EventType(reg.event_type));
    }

    RegistrationId add(const CameraRegistration &reg, EventCallback callback)
    {
        const std::string key = cameraKey(reg);
        const RegistrationId id = nextId++;
        callbacks[key].push_back({id, std::move(callback)});
        registrations.insert_or_assign(key, reg); //not operator[]: see CameraRegistration
        idToKey[id] = key;
        return id;
    }

    /*
     * Remove one callback. Returns true when it was the last callback for
     * its camera key: the whole camera registration record is dropped too.
     * Unknown ids (including INVALID_ID) are ignored and return false.
     */
    bool remove(RegistrationId id, std::string *removedCameraKey = nullptr)
    {
        auto idIt = idToKey.find(id);
        if (idIt == idToKey.end())
            return false;

        const std::string key = idIt->second;
        idToKey.erase(idIt);

        auto cbIt = callbacks.find(key);
        if (cbIt != callbacks.end())
        {
            auto &v = cbIt->second;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [id](const Entry &e) { return e.id == id; }),
                    v.end());
            if (!v.empty())
                return false;
            callbacks.erase(cbIt);
        }

        registrations.erase(key);
        if (removedCameraKey)
            *removedCameraKey = key;
        return true;
    }

    /*
     * Deliver an event to every callback registered for hostname/event_type.
     * Returns the number of callbacks actually invoked.
     *
     * Safe against reentrant modification: it iterates over a copy of the
     * callback list (a callback may register/unregister IOs), and re-checks
     * that each id is still registered before invoking it, so a callback
     * unregistered by an earlier callback of the same dispatch (e.g. its IO
     * was just deleted) is never invoked on a dead object.
     */
    size_t dispatch(const ReolinkTypes::Hostname &hostname,
                    const ReolinkTypes::EventType &event_type,
                    const ReolinkTypes::EventData &event_data)
    {
        auto it = callbacks.find(cameraKey(hostname, event_type));
        if (it == callbacks.end())
            return 0;

        const std::vector<Entry> copy = it->second;
        size_t delivered = 0;
        for (const auto &e : copy)
        {
            if (idToKey.find(e.id) == idToKey.end())
                continue; //unregistered while this dispatch was running
            e.callback(hostname, event_type, event_data);
            delivered++;
        }
        return delivered;
    }

    bool hasCamera(const ReolinkTypes::Hostname &hostname,
                   const ReolinkTypes::EventType &event_type) const
    {
        return registrations.count(cameraKey(hostname, event_type)) > 0;
    }

    size_t callbackCount(const ReolinkTypes::Hostname &hostname,
                         const ReolinkTypes::EventType &event_type) const
    {
        auto it = callbacks.find(cameraKey(hostname, event_type));
        return it == callbacks.end() ? 0 : it->second.size();
    }

    bool empty() const { return registrations.empty(); }
    size_t cameraCount() const { return registrations.size(); }

    template<typename F>
    void forEachRegistration(F &&fn) const
    {
        for (const auto &p : registrations)
            fn(p.second);
    }

private:
    struct Entry
    {
        RegistrationId id;
        EventCallback callback;
    };

    RegistrationId nextId = 1;
    std::unordered_map<std::string, std::vector<Entry>> callbacks;
    std::unordered_map<std::string, CameraRegistration> registrations;
    std::unordered_map<RegistrationId, std::string> idToKey;
};

#endif // __REOLINK_EVENT_REGISTRY_H__
