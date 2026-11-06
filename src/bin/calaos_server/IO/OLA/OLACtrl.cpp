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
#include "OLACtrl.h"
#include "OLAWire.h"
#include "Prefix.h"

OLACtrl::OLACtrl(const string &universe)
{
    cDebugDom("ola") << "new OLACtrl: " << universe;
    process = new ExternProcServer("ola");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_ola";

    //An unset universe used to vanish in the re-split, and calaos_ola keeps its
    //own default when no argument reaches it. Passing an empty argv instead
    //would make it read "" as a universe number.
    vector<string> args;
    if (!universe.empty())
        args.push_back(universe);

    process->processExited.connect([=]()
                                       {
                                           //restart process when stopped
                                           cWarningDom("process") << "process exited, restarting...";
                                           process->startProcess(exe, "ola", args);
                                       });

    process->startProcess(exe, "ola", args);
}

OLACtrl::~OLACtrl()
{
    delete process;
}

/*
 * E4.1f: the assembly lives in OLAWire.h, which calaos_ola includes too, so
 * that tests/OLAWire_test.cpp exercises the SHIPPED emitter and not a copy of
 * it. The *255/100 that turns the 0-100 setting into a DMX level moved there
 * with it - it is part of what goes on the wire.
 *
 * ⭐ The two arguments are wrapped in DISTINCT types on the way in. That is
 * the point: `channel` and `value` are both plain ints here, a free function
 * cannot cover its own call site, and permuting two positional int arguments
 * was measured to stay green in three tickets of this series. Written this
 * way, the swap does not compile.
 */
void OLACtrl::setValue(int channel, int value)
{
    const string res = OLAWire::buildSetValueMessage(OLAWire::DmxChannel(channel),
                                                     OLAWire::DimmerPercent(value));

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("ola") << "Sending value (" << value << ") " << res;
}

/*
 * E4.1f: same move, and the ColorValue is handed over WHOLE. The pairing of a
 * component with its channel happens inside buildSetColorMessage(), where the
 * test suite can see it, instead of three times here where nothing could.
 *
 * ⭐ And the three channels are wrapped in three DISTINCT types, so handing
 * the blue channel where the red one belongs does not compile either - the
 * exact swap acceptance criterion 4 of E4.1f asks about.
 */
void OLACtrl::setColor(const ColorValue &color, int channel_red, int channel_green, int channel_blue)
{
    const string res = OLAWire::buildSetColorMessage(color,
                                                     OLAWire::RedChannel(channel_red),
                                                     OLAWire::GreenChannel(channel_green),
                                                     OLAWire::BlueChannel(channel_blue));

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("ola") << "Sending: " << res;
}

shared_ptr<OLACtrl> OLACtrl::Instance(const string &universe)
{
    static map<string, shared_ptr<OLACtrl>> mapInst;
    auto it = mapInst.find(universe);
    if (it != mapInst.end())
        return it->second;

    shared_ptr<OLACtrl> inst(new OLACtrl(universe));
    mapInst[universe] = std::move(inst);
    return mapInst[universe];
}
