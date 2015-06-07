/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "OpenZwaveCtrl.h"
#include "Prefix.h"

OpenZwaveCtrl::OpenZwaveCtrl(const string &id)
{
    cDebugDom("openzwave") << "new OpenZwaveCtrl: " << id;
    process = new ExternProcServer("openzwave");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_openzwave";

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "openzwave", id);
    });

    process->startProcess(exe, "openzwave", id);
}

OpenZwaveCtrl::~OpenZwaveCtrl()
{
    delete process;
}

void OpenZwaveCtrl::setValue(int channel, int value)
{
    json_t *jroot = json_array();

    json_t *jdata = json_object();
    json_object_set_new(jdata, "channel", json_integer(channel));
    json_object_set_new(jdata, "value", json_integer(value * 255 / 100));
    json_array_append_new(jroot, jdata);

    string res = jansson_to_string(jroot);

    if (!res.empty())
        process->sendMessage(res);

    cDebugDom("openzwave") << "Sending value (" << value << ") " << res;
}

void OpenZwaveCtrl::setColor(const ColorValue &color, int channel_red, int channel_green, int channel_blue)
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

    cDebugDom("openzwave") << "Sending: " << res;
}

shared_ptr<OpenZwaveCtrl> OpenZwaveCtrl::Instance(const string &id)
{
    static map<string, shared_ptr<OpenZwaveCtrl>> mapInst;
    auto it = mapInst.find(id);
    if (it != mapInst.end())
        return it->second;

    shared_ptr<OpenZwaveCtrl> inst(new OpenZwaveCtrl(id));
    mapInst[id] = std::move(inst);
    return mapInst[id];
}
