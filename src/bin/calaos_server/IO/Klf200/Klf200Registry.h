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
#ifndef S_KLF200_REGISTRY_H
#define S_KLF200_REGISTRY_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Params.h"
#include "Klf200Types.h"

/* Callbacks and registrations of the VELUX nodes, kept free of any process
 * or loop so it can be unit tested. One record per node key, shared by every
 * IO of that node (shutter + rain sensor of one window). */
class Klf200Registry
{
public:
    using RegistrationId = uint64_t;
    static constexpr RegistrationId INVALID_ID = 0;

    /* The whole decoded message, flattened by Klf200Wire::decodeMessage(). */
    using EventCallback = std::function<void(const Params &event)>;

    struct NodeRegistration
    {
        NodeRegistration(Klf200Types::Host h,
                         Klf200Types::Password p,
                         Klf200Types::NodeId n,
                         bool r):
            host(std::move(h.v)),
            password(std::move(p.v)),
            nodeId(n.v),
            rain(r)
        {
        }

        std::string host;
        std::string password;
        int nodeId;
        bool rain;
    };

    static std::string nodeKey(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId)
    {
        return host.v + "/" + std::to_string(nodeId.v);
    }

    static std::string nodeKey(const NodeRegistration &reg)
    {
        return nodeKey(Klf200Types::Host(reg.host), Klf200Types::NodeId(reg.nodeId));
    }

    /* The first registration of a key fixes its password (one gateway, one
     * password: the first IO wins, Klf200Ctrl logs a disagreement); the rain
     * flag accumulates. */
    RegistrationId add(const NodeRegistration &reg, EventCallback callback)
    {
        const std::string key = nodeKey(reg);
        const RegistrationId id = nextId++;
        callbacks[key].push_back({ id, std::move(callback) });

        auto it = registrations.find(key);
        if (it == registrations.end())
            registrations.insert_or_assign(key, reg);
        else
            it->second.rain = it->second.rain || reg.rain;

        idToKey[id] = key;
        return id;
    }

    /*
     * Remove one callback. Returns true when it was the last callback for
     * its node key: the whole record is dropped too. Unknown ids (including
     * INVALID_ID) are ignored and return false.
     */
    bool remove(RegistrationId id, std::string *removedNodeKey = nullptr)
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
            for (auto e = v.begin(); e != v.end(); ++e)
            {
                if (e->id == id)
                {
                    v.erase(e); //ids are unique
                    break;
                }
            }
            if (!v.empty())
                return false;
            callbacks.erase(cbIt);
        }

        registrations.erase(key);
        if (removedNodeKey)
            *removedNodeKey = key;
        return true;
    }

    /*
     * Deliver an event to every callback registered for host/node_id.
     * Returns the number of callbacks actually invoked.
     *
     * Safe against reentrant modification: it iterates over a copy of the
     * callback list (a callback may register/unregister IOs), and re-checks
     * that each id is still registered before invoking it.
     */
    size_t dispatch(const Klf200Types::Host &host,
                    const Klf200Types::NodeId &nodeId,
                    const Params &event)
    {
        auto it = callbacks.find(nodeKey(host, nodeId));
        if (it == callbacks.end())
            return 0;

        const std::vector<Entry> copy = it->second;
        size_t delivered = 0;
        for (const auto &e: copy)
        {
            if (idToKey.find(e.id) == idToKey.end())
                continue; //unregistered while this dispatch was running
            e.callback(event);
            delivered++;
        }
        return delivered;
    }

    bool hasNode(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId) const
    {
        return registration(host, nodeId) != nullptr;
    }

    //The merged record of a node (what is sent to the process), or nullptr
    const NodeRegistration *registration(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId) const
    {
        auto it = registrations.find(nodeKey(host, nodeId));
        return it == registrations.end() ? nullptr : &it->second;
    }

    //The record a registration id belongs to, or nullptr
    const NodeRegistration *registrationOf(RegistrationId id) const
    {
        auto idIt = idToKey.find(id);
        if (idIt == idToKey.end())
            return nullptr;
        auto it = registrations.find(idIt->second);
        return it == registrations.end() ? nullptr : &it->second;
    }

    /* Whether a record of the same host already holds another password: the
     * gateway has only one, and the first registered is the one kept. */
    bool passwordConflicts(const NodeRegistration &reg) const
    {
        for (const auto &kv: registrations)
            if (kv.second.host == reg.host && kv.second.password != reg.password)
                return true;
        return false;
    }

    /* Whether any registration names this host: what a sidecar echoes back
     * is not, on its own, a value this end can vouch for in a log line. */
    bool hasHost(const std::string &host) const
    {
        for (const auto &kv: registrations)
            if (kv.second.host == host)
                return true;
        return false;
    }

    bool empty() const { return registrations.empty(); }
    size_t nodeCount() const { return registrations.size(); }

    void forEachRegistration(const std::function<void(const NodeRegistration &)> &fn) const
    {
        for (const auto &kv: registrations)
            fn(kv.second);
    }

private:
    struct Entry
    {
        RegistrationId id;
        EventCallback callback;
    };

    RegistrationId nextId = 1;
    std::unordered_map<std::string, std::vector<Entry>> callbacks;
    std::unordered_map<std::string, NodeRegistration> registrations;
    std::unordered_map<RegistrationId, std::string> idToKey;
};

/* Whether a command can reach a gateway: process connected AND that
 * gateway's session up, so an IO refuses an order instead of faking success. */
class Klf200GatewayLinks
{
public:
    //The process (re)connected: no gateway session is known to be up yet
    void processConnected()
    {
        process = true;
        gateways.clear();
    }

    //The process exited: every session went with it
    void processExited()
    {
        process = false;
        gateways.clear();
    }

    void gatewayStatus(const std::string &host, bool connected)
    {
        if (connected)
            gateways.insert(host);
        else
            gateways.erase(host);
    }

    bool processUp() const { return process; }

    bool canSend(const std::string &host) const
    {
        return process && gateways.count(host) > 0;
    }

private:
    bool process = false;
    std::set<std::string> gateways;
};

#endif
