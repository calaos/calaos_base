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
#ifndef S_KLF200_CTRL_H
#define S_KLF200_CTRL_H

#include <set>

#include "Calaos.h"
#include "ExternProc.h"
#include "Params.h"
#include "Utils.h"
#include "Klf200Registry.h"
#include "Klf200Types.h"

/*
 * Owner of the calaos_klf200 external process (IO/Klf200/ExternProcKlf200_main.py
 * through the configure-generated wrapper). One process for the whole server,
 * one pyvlx session per KLF 200 host inside it.
 *
 * IOs register the node they drive with a callback; the controller forwards
 * the registration to the process, re-sends every registration when the
 * process (re)connects, and dispatches the node events it gets back.
 */
class Klf200Ctrl: public sigc::trackable
{
public:
    using RegistrationId = Klf200Registry::RegistrationId;
    using EventCallback = Klf200Registry::EventCallback;

private:
    Klf200Ctrl();
    ~Klf200Ctrl();

    ExternProcServer *process;
    string exe;

    Klf200Registry registry;

    //Process and per-gateway session state, as the process reports it
    Klf200GatewayLinks links;

    void doRegisterNode(const Klf200Registry::NodeRegistration &reg);
    void registerAllNodes();
    bool canSend(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId, const char *what) const;

public:
    // Returns an id that MUST be passed to unregisterNode() before the
    // callback's owner is destroyed, otherwise the stored callback dangles.
    RegistrationId registerNode(Klf200Types::Host host,
                                Klf200Types::Password password,
                                Klf200Types::NodeId nodeId,
                                bool rain,
                                EventCallback callback);
    void unregisterNode(RegistrationId id);

    /* 0 = fully open, 100 = fully closed. Both answer false, and send
     * nothing, while the process or that gateway's session is down: the
     * caller must not report the order as taken. */
    bool setPosition(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId, int position);
    bool stop(const Klf200Types::Host &host, const Klf200Types::NodeId &nodeId);

    bool isConnected() const { return links.processUp(); }

    static Klf200Ctrl &Instance()
    {
        static Klf200Ctrl instance;
        return instance;
    }
};

#endif
