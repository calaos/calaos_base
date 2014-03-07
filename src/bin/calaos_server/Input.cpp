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
#include <Input.h>
#include <ListeRoom.h>
#include <ListeRule.h>
#include <DataLogger.h>

using namespace Calaos;

Input::Input(Params &p):
    IOBase(p)
{
    iter_input = signal_input.connect(sigc::mem_fun(&ListeRule::Instance(), &ListeRule::ExecuteRuleSignal));

    ListeRoom::Instance().addInputHash(this);
}

Input::~Input()
{
    iter_input->disconnect();
    ListeRoom::Instance().delInputHash(this);
}

void Input::EmitSignalInput()
{
    cDebugDom("input") << "Input::EmitSignalInput(" << get_param("id") << ")";
    signal_input.emit(get_param("id"));
    DataLogger::Instance().log(this);
}

bool Input::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:input");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}
