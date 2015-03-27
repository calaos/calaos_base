/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "OWCtrl.h"
#include "Prefix.h"

OwCtrl::OwCtrl(const string &args)
{
    process = new ExternProcServer("1wire");

    exe = Prefix::Instance().binDirectoryGet() + "/calaos_1wire";

    process->messageReceived.connect(sigc::mem_fun(*this, &OwCtrl::processNewMessage));

    process->processExited.connect([=]()
    {
        //restart process when stopped
        cWarningDom("process") << "process exited, restarting...";
        process->startProcess(exe, "1wire", args);
    });

    process->startProcess(exe, "1wire", args);
}

OwCtrl::~OwCtrl()
{
    delete process;
}

string OwCtrl::getValue(string owid)
{
    return mapValues[owid];
}

string OwCtrl::getType(string owid)
{
    return mapTypes[owid];
}

shared_ptr<OwCtrl> OwCtrl::Instance(const string &args)
{
    static map<string, shared_ptr<OwCtrl>> mapInst;
    auto it = mapInst.find(args);
    if (it != mapInst.end())
        return it->second;

    shared_ptr<OwCtrl> inst(new OwCtrl(args));
    mapInst[args] = std::move(inst);
    return mapInst[args];
}

void OwCtrl::processNewMessage(const string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_array(jroot))
    {
        cWarningDom("1wire") << "Error parsing json from sub process: " << jerr.text;
        if (jroot)
            json_decref(jroot);
        return;
    }

    int idx;
    json_t *value;

    json_array_foreach(jroot, idx, value)
    {
        Params p;
        jansson_decode_object(value, p);

        if (p.Exists("id"))
        {
            if (p.Exists("value"))
                mapValues[p["id"]] = p["value"];
            if (p.Exists("type"))
                mapValues[p["id"]] = p["type"];
        }
    }

    valueChanged.emit();
}
