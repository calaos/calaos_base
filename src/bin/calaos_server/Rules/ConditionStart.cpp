/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "ConditionStart.h"

using namespace Calaos;

ConditionStart::ConditionStart():
    Condition(COND_START),
    start(true)
{
    cDebugDom("rule.condition.start") <<  "New Start condition";
}

ConditionStart::~ConditionStart()
{
}

bool ConditionStart::Evaluate()
{
    if (start)
    {
        start = false;
        cDebugDom("rule.condition.start") <<  "calaosd is starting, true";

        return true;
    }
    else
    {
        cDebugDom("rule.condition.start") <<  "calaosd is already started, false";
    }

    return false;
}

bool ConditionStart::LoadFromXml(pugi::xml_node node)
{
    return true;
}

bool ConditionStart::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cond_node = node.append_child("calaos:condition");
    XmlUtils::setAttribute(cond_node, "type", "start");

    return true;
}
