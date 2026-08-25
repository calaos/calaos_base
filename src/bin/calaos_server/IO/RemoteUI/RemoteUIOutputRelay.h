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
#ifndef REMOTEUIOUTPUTRELAY_H
#define REMOTEUIOUTPUTRELAY_H

#include "OutputLight.h"

namespace Calaos
{

class RemoteUIOutputRelay : public OutputLight
{
private:
    string remote_ui_id;
    int relay_num;

protected:
    virtual bool set_value_real(bool val) override;

public:
    RemoteUIOutputRelay(Params &p);
    virtual ~RemoteUIOutputRelay() = default;

    /* T3.25 (review reserve 1). The relay number this IO decided to drive,
     * after the "relay_num" parameter has been parsed. Purely additive and
     * const: it changes no behaviour and nothing in src/ calls it.
     *
     * It exists because the parsed value had NO OBSERVABLE at all. The only
     * production consumer is set_value_real(), which hands it to
     * RemoteUIManager::sendCommand(), and that returns early with a warning
     * unless a RemoteUIWebSocketHandler is connected - i.e. unless a real
     * device holds a live websocket. So a missing or blank "relay_num"
     * silently drove relay 0 - a relay the ioDoc says does not exist (1..99) -
     * and no test in the tree could see it. core/RemoteUIDeviceInfo_test pins
     * the three shapes through this accessor.
     */
    int getRelayNum() const { return relay_num; }

    // Update internal state from a device-initiated change without sending a command back
    void updateStateFromDevice(bool val);
};

}

#endif // REMOTEUIOUTPUTRELAY_H
