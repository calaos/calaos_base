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
#include "KNXBase.h"
#include "IOFactory.h"

using namespace Calaos;

KNXBase::KNXBase(Params *p, IODoc *ioDoc, bool add_doc_group):
    params(p)
{
    // Define common KNX IO documentation
    ioDoc->linkAdd("knxd", _("https://github.com/knxd/knxd/g"));
    if (add_doc_group)
    {
        ioDoc->paramAdd("knx_group", _("KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
        ioDoc->paramAdd("listen_knx_group", _("KNX Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
    }
    ioDoc->paramAddInt("eis", _("KNX EIS (Data type)"), 0, 15, false, KNXValue::EIS_Value_Int);
    ioDoc->paramAdd("read_at_start", _("Send a read request at start to get the current value. Default is false"), IODoc::TYPE_BOOL, true, "false");
    ioDoc->paramAdd("host", _("Hostname of knxd, default to localhost"), IODoc::TYPE_STRING, true, "127.0.0.1");
}

KNXBase::~KNXBase()
{
}

string KNXBase::getReadGroupAddr(const string &group_base)
{
    string knx_group = params->get_param(group_base);
    if (params->Exists("listen_" + group_base))
        knx_group = params->get_param("listen_" + group_base);

    return knx_group;
}

