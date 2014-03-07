/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <IPCam.h>

using namespace Calaos;

IPCam::IPCam(Params &p): param(p)
{
    if (!param.Exists("port")) param.Add("port", "80");

    Params pio = param;

    pio.Add("id", param["oid"]);
    pio.Add("type", "CamOutput");
    aoutput = new CamOutput(pio, this);

    pio.Add("id", param["iid"]);
    pio.Add("type", "CamInput");
    ainput = new CamInput(pio, this);
}

IPCam::~IPCam()
{
    delete aoutput;
    delete ainput;
}

bool IPCam::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:camera");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        param.get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}
