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
#include "Klf200Ctrl.h"
#include "Klf200Wire.h"
#include "Prefix.h"

using namespace Calaos;

static const char *TAG = "klf200";

Klf200Ctrl::Klf200Ctrl()
{
    cDebugDom(TAG) << "New Klf200 external process";
    process = new ExternProcServer("klf200");
    exe = Prefix::Instance().binDirectoryGet() + "/calaos_klf200";

    process->processExited.connect([this]()
                                   {
        //restart process when stopped
        cWarningDom(TAG) << "Klf200 process exited, restarting...";
        links.processExited();
        process->startProcess(exe, "klf200");
    });

    process->messageReceived.connect([this](const string &msg)
                                     {
        Params p;

        //Never log msg itself: this channel carries the gateway password.
        if (!Klf200Wire::decodeMessage(msg, p))
        {
            cWarningDom(TAG) << "Error parsing json message (" << msg.size() << " bytes)";
            return;
        }

        if (p.Exists("status"))
        {
            const string status = p["status"];
            const string host = p["host"];
            const bool knownHost = registry.hasHost(host);

            if (status == "connected")
            {
                links.processConnected();
                cInfoDom(TAG) << "Klf200 process connected";

                //Re-register every node when the process (re)connects
                registerAllNodes();
            }
            else if (status == "gateway")
            {
                /* Only a host this end registered is tracked: anything else
                 * could never be the target of a command anyway. */
                if (knownHost)
                    links.gatewayStatus(host, p["connected"] == "true");
                cInfoDom(TAG) << "KLF 200 " << (knownHost ? host : string("unknown"))
                              << (p["connected"] == "true" ? " connected" : " disconnected");
            }
            else if (status == "error")
            {
                /* "message" is the text of a pyvlx exception: nothing in this
                 * tree constrains it, so only a code of the wire vocabulary
                 * and a host this controller registered itself cross. */
                const string code = p["code"];
                cErrorDom(TAG) << "Klf200 process error: "
                               << (Klf200Wire::isKnownErrorCode(code) ? code : string("unspecified"))
                               << ", gateway " << (knownHost ? host : string("unknown"))
                               << ", node " << Klf200Wire::nodeIdOf(p)
                               << " (" << p["message"].size() << " bytes withheld)";
            }
        }
        else if (p.Exists("event") && p.Exists("host"))
        {
            const string host = p["host"];
            const int nodeId = Klf200Wire::nodeIdOf(p);

            if (nodeId < 0)
            {
                cWarningDom(TAG) << "Event without a valid node_id, dropped";
                return;
            }

            cDebugDom(TAG) << "Event " << p["event"] << " from " << host << " node " << nodeId
                           << " position=" << p["position"] << " moving=" << p["moving"]
                           << " rain=" << p["rain"];

            registry.dispatch(Klf200Types::Host(host), Klf200Types::NodeId(nodeId), p);
        }
    });

    process->startProcess(exe, "klf200");
}

Klf200Ctrl::~Klf200Ctrl()
{
}

Klf200Ctrl::RegistrationId Klf200Ctrl::registerNode(Klf200Types::Host host,
                                                    Klf200Types::Password password,
                                                    Klf200Types::NodeId nodeId,
                                                    bool rain,
                                                    EventCallback callback)
{
    const Klf200Registry::NodeRegistration reg(std::move(host),
                                               std::move(password),
                                               nodeId,
                                               rain);
    const string key = Klf200Registry::nodeKey(reg);

    cDebugDom(TAG) << "Registering node " << key << (rain ? " (rain sensor)" : "");

    RegistrationId id = registry.add(reg, std::move(callback));

    /* Sent even when another IO already registered this node: the sidecar
     * merges the registration and answers every register with the current
     * node state (and a fresh rain reading when asked), which is the only way
     * an IO created while the process runs learns the state before the node
     * next changes. */
    if (links.processUp())
        doRegisterNode(reg);
    else
        cDebugDom(TAG) << "Process not connected, node will be registered when it connects";

    return id;
}

void Klf200Ctrl::unregisterNode(RegistrationId id)
{
    string key;
    if (registry.remove(id, &key))
    {
        //Last callback for this node gone. The external process keeps
        //watching the node until it restarts, which is harmless: its events
        //find no callback here.
        cDebugDom(TAG) << "Last callback removed for node " << key;
    }
}

void Klf200Ctrl::doRegisterNode(const Klf200Registry::NodeRegistration &reg)
{
    const string key = Klf200Registry::nodeKey(reg);

    /* The message carries the gateway password IN CLEAR: never log it. */
    process->sendMessage(Klf200Wire::buildRegisterMessage(Klf200Types::Host(reg.host),
                                                          Klf200Types::Password(reg.password),
                                                          Klf200Types::NodeId(reg.nodeId),
                                                          reg.rain));

    cInfoDom(TAG) << "Node registration sent: " << key;
}

void Klf200Ctrl::registerAllNodes()
{
    if (registry.empty())
    {
        cDebugDom(TAG) << "No nodes to register";
        return;
    }

    cInfoDom(TAG) << "Registering " << registry.nodeCount() << " nodes to process";

    registry.forEachRegistration([this](const Klf200Registry::NodeRegistration &reg)
                                 {
        doRegisterNode(reg);
    });
}

bool Klf200Ctrl::canSend(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId, const char *what) const
{
    if (links.canSend(host.v))
        return true;

    cErrorDom(TAG) << (links.processUp() ? "KLF 200 gateway not connected" : "Klf200 process not connected")
                   << ", " << what << " on node " << Klf200Registry::nodeKey(host, nodeId) << " refused";
    return false;
}

bool Klf200Ctrl::setPosition(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId, int position)
{
    if (!canSend(host, nodeId, "set_position"))
        return false;

    process->sendMessage(Klf200Wire::buildSetPositionMessage(host, nodeId, position));
    return true;
}

bool Klf200Ctrl::stop(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId)
{
    if (!canSend(host, nodeId, "stop"))
        return false;

    process->sendMessage(Klf200Wire::buildStopMessage(host, nodeId));
    return true;
}
