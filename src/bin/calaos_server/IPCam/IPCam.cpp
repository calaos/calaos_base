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
#include "IPCam.h"


namespace Calaos {

IPCam::IPCam(Params &p):
    IOBase(p, IOBase::IO_INOUT)
{
    //TODO add IODoc

    if (!param_exists("port"))
        set_param("port", "80");

    set_param("gui_type", "camera");
    set_param("visible", "false");
}

IPCam::~IPCam()
{
}

bool IPCam::set_value(std::string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << "CamOutput(" << get_param("id") << "): got action, " << val;

    if (val.compare(0, 5, "move ") == 0)
    {
        val.erase(0, 5);
        activateCapabilities("ptz", "move", val);
    }
    else if (val.compare(0, 5, "save ") == 0)
    {
        val.erase(0, 5);
        activateCapabilities("position", "save", val);
    }
    else if (val.compare(0, 7, "recall ") == 0)
    {
        val.erase(0, 7);
        activateCapabilities("position", "recall", val);
    }

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", val } });

    return true;
}

bool IPCam::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:camera");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        std::string key, value;
        param.get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

}
