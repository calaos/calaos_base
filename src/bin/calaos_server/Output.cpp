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
#include <Output.h>
#include <ListeRoom.h>
#include <DataLogger.h>

using namespace Calaos;

Output::Output(Params &p):
    IOBase(p)
{
    iter_output = signal_output.connect(sigc::mem_fun(&ListeRule::Instance(), &ListeRule::ExecuteRuleSignal));

    ListeRoom::Instance().addOutputHash(this);
}

Output::~Output()
{
    iter_output->disconnect();
    ListeRoom::Instance().delOutputHash(this);
}

void Output::EmitSignalOutput()
{
    cDebugDom("output") << get_param("id");
    signal_output.emit(get_param("id"));
    DataLogger::Instance().log(this);
}

bool Output::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:output");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}
