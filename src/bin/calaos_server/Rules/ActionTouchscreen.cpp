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
#include <ActionTouchscreen.h>

using namespace Calaos;

ActionTouchscreen::ActionTouchscreen():
    Action(ACTION_TOUCHSCREEN),
    econ(NULL)
{
    econ = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST, "255.255.255.255", BCAST_UDP_PORT, NULL);

    cDebugDom("rule.action.touchscreen") <<  "ActionTouchscreen::ActionTouchscreen(): New Touchscreen action";
}

ActionTouchscreen::ActionTouchscreen(string _action): Action(ACTION_TOUCHSCREEN), action(_action)
{
    econ = ecore_con_server_connect(ECORE_CON_REMOTE_BROADCAST, "255.255.255.255", BCAST_UDP_PORT, NULL);

    cDebugDom("rule.action.touchscreen") <<  "ActionTouchscreen::ActionTouchscreen(): New Touchscreen action";
}

ActionTouchscreen::~ActionTouchscreen()
{
    ecore_con_server_del(econ);

    cDebugDom("rule.action.touchscreen") <<  "ActionTouchscreen::~ActionTouchscreen(): Ok";
}

bool ActionTouchscreen::Execute()
{
    if (action.substr(0, 9) == "show,cam,")
    {
        ecore_con_server_send(econ, action.c_str(), action.size());

        cDebugDom("rule.action.touchscreen") <<  "ActionTouchscreen::Execute(): Show camera";

        return true;
    }
    else
    {
        cWarningDom("rule.action.touchscreen") <<  "ActionTouchscreen::Execute(): Unknown action !";
    }

    return false;
}

bool ActionTouchscreen::LoadFromXml(TiXmlElement *pnode)
{
    if (pnode->Attribute("action"))
        action = pnode->Attribute("action");

    return true;
}

bool ActionTouchscreen::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "touchscreen");
    action_node->SetAttribute("action", action);
    node->LinkEndChild(action_node);

    return true;
}
