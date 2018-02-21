/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "Prefix.h"

OLACtrl::OLACtrl(const string &universe)
{
    cDebugDom("ola") << "new OLACtrl: " << universe;
    process = new ExternProcServer("ola");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_ola";

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "ola", universe);
    });

    process->startProcess(exe, "ola", universe);
}

OLACtrl::~OLACtrl()
{
    delete process;
}

void OLACtrl::setValue(int channel, int value)
{
    json_t *jroot = json_array();

    json_t *jdata = json_object();
    json_object_set_new(jdata, "channel", json_integer(channel));
    json_object_set_new(jdata, "value", json_integer(value * 255 / 100));
    json_array_append_new(jroot, jdata);

    string res = jansson_to_string(jroot);

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("ola") << "Sending value (" << value << ") " << res;
}

void OLACtrl::setColor(const ColorValue &color, int channel_red, int channel_green, int channel_blue)
{
    json_t *jroot = json_array();

    json_t *jdata = json_object();
    json_object_set_new(jdata, "channel", json_integer(channel_red));
    json_object_set_new(jdata, "value", json_integer(color.getRed()));
    json_array_append_new(jroot, jdata);

    jdata = json_object();
    json_object_set_new(jdata, "channel", json_integer(channel_green));
    json_object_set_new(jdata, "value", json_integer(color.getGreen()));
    json_array_append_new(jroot, jdata);

    jdata = json_object();
    json_object_set_new(jdata, "channel", json_integer(channel_blue));
    json_object_set_new(jdata, "value", json_integer(color.getBlue()));
    json_array_append_new(jroot, jdata);

    string res = jansson_to_string(jroot);

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
