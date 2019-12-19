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
#include "IOBase.h"
#include "ListeRoom.h"
#include "ListeRule.h"
#include "DataLogger.h"

using namespace Calaos;

IOBase::IOBase(Params &p, int iotype):
    param(p),
    auto_sc_mark(false),
    io_type(iotype)
{
    ioDoc = new IODoc();
    ioDoc->paramAdd("id", _("Unique ID identifying the Input/Output in calaos-server"), IODoc::TYPE_STRING, true, string(), true);
    ioDoc->paramAdd("name", _("Name of Input/Output."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("visible", _("Display the Input/Output on all user interfaces if set. Default to true"), IODoc::TYPE_BOOL, false, "true");
    ioDoc->paramAdd("enabled", _("Enable the Input/Output. The default value is true. This parameter is added if it's not found in the configuration."), IODoc::TYPE_BOOL, false, "true");
    ioDoc->paramAdd("gui_type", _("Internal graphical type for all calaos objects. Set automatically, read-only parameter."), IODoc::TYPE_STRING, false, string(), true);
    ioDoc->paramAdd("io_type", _("IO type, can be \"input\", \"output\", \"inout\""), IODoc::TYPE_STRING, true, string(), true);
    ioDoc->paramAdd("log_history", _("If enabled, write an entry in the history event log for this IO"), IODoc::TYPE_BOOL, false, "false");
    ioDoc->paramAdd("logged", _("If enabled, and if influxdb is enabled in local_config send the value to influxdb for this IO"), IODoc::TYPE_BOOL, false, "false");

    if (!param.Exists("enabled"))
        param.Add("enabled", "true");

    param.Add("io_type", io_type == IO_INPUT?"input":io_type == IO_OUTPUT?"output":"inout");

    ListeRoom::Instance().addIOHash(this);
}

IOBase::~IOBase()
{
    ListeRoom::Instance().delIOHash(this);
    delete ioDoc;
}

void IOBase::EmitSignalIO()
{
    cDebugDom("iobase") << get_param("id");
    ListeRule::Instance().ExecuteRuleSignal(get_param("id"));
    DataLogger::Instance().log(this);
}

bool IOBase::LoadFromXml(TiXmlElement *node)
{
    VAR_UNUSED(node);
    return true;
}

bool IOBase::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode;
    if (isInput())
        cnode = new TiXmlElement("calaos:input");
    else
        cnode = new TiXmlElement("calaos:output");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}
