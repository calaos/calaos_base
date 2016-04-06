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

namespace Calaos {
OwCtrl::OwCtrl(const std::string &args)
{
    cDebugDom("1wire") << "new OWCtrl: " << args;
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

std::string OwCtrl::getValue(std::string owid)
{
    return mapValues[owid];
}

std::string OwCtrl::getType(std::string owid)
{
    return mapTypes[owid];
}

std::shared_ptr<OwCtrl> OwCtrl::Instance(const std::string &args)
{
    static std::map<std::string, std::shared_ptr<OwCtrl>> mapInst;
    auto it = mapInst.find(args);
    if (it != mapInst.end())
        return it->second;

    std::shared_ptr<OwCtrl> inst(new OwCtrl(args));
    mapInst[args] = std::move(inst);
    return mapInst[args];
}

void OwCtrl::processNewMessage(const std::string &msg)
{
    Json jroot;
    try
    {
        jroot = Json::parse(msg);
        if (!jroot.is_array())
           throw (std::invalid_argument(std::string("Json is not an array: ") + msg));
    }
    catch (const std::exception &e)
    {
        cWarningDom("1wire") << "Error parsing json from sub process: " << e.what();
        return;
    }

    for (auto it: jroot)
    {
        if (it.find("id") != it.end())
        {
            if (it.find("value") != it.end())
                mapValues[it["id"].get<std::string>()] = it["value"].get<std::string>();
            if (it.find("type") != it.end())
                mapTypes[it["id"].get<std::string>()] = it["type"].get<std::string>();
        }
    }

    valueChanged.emit();
}

}
