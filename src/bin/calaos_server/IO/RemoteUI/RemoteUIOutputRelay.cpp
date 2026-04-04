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
#include "RemoteUIOutputRelay.h"
#include "IOFactory.h"
#include "RemoteUIManager.h"

static const char *TAG = "remote_ui";

using namespace Calaos;

REGISTER_IO(RemoteUIOutputRelay)

RemoteUIOutputRelay::RemoteUIOutputRelay(Params &p):
    OutputLight(p),
    relay_num(1)
{
    ioDoc->friendlyNameSet("RemoteUIOutputRelay");
    ioDoc->descriptionBaseSet(_("Physical relay on a RemoteUI panel device. Controllable as a standard light output."));
    ioDoc->paramAdd("remote_ui_id", _("ID of the parent RemoteUI device"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("relay_num", _("Relay number on the device (1, 2, ...)"), 1, 99, true);

    remote_ui_id = get_param("remote_ui_id");
    Utils::from_string(get_param("relay_num"), relay_num);

    cInfoDom(TAG) << "RemoteUIOutputRelay(" << get_param("id") << "): relay " << relay_num
                  << " on device " << remote_ui_id;
}

bool RemoteUIOutputRelay::set_value_real(bool val)
{
    RemoteUIManager::Instance().sendCommand(remote_ui_id, "remote_ui_set_relay",
        { { "relay", relay_num }, { "state", val } });
    return true;
}

void RemoteUIOutputRelay::updateStateFromDevice(bool val)
{
    // Update internal state from a device-initiated change without sending the
    // command back to the device, which would cause a feedback loop.
    bool hasChanged = value != val;
    value = val;

    if (hasChanged)
    {
        EmitSignalIO();
        emitChange();
    }
}
