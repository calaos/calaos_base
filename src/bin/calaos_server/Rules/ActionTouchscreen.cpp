/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "ActionTouchscreen.h"
#include "EventManager.h"
#include "ListeRoom.h"

using namespace Calaos;

ActionTouchscreen::ActionTouchscreen():
    Action(ACTION_TOUCHSCREEN)
{
    cDebugDom("rule.action.touchscreen") <<  "New Touchscreen action";
}

ActionTouchscreen::~ActionTouchscreen()
{
}

bool ActionTouchscreen::Execute()
{
    IOBase *io = ListeRoom::Instance().get_io(cameraId);

    if (!io)
    {
        cWarningDom("rule.action.touchscreen") << "Unable to find camera " << cameraId << ". Can't start action.";
        return false;
    }

    cDebugDom("rule.action.touchscreen") <<  "Show camera";

    EventManager::create(CalaosEvent::EventTouchScreenCamera,
                         { { "id", cameraId } });

    return true;
}

bool ActionTouchscreen::LoadFromXml(TiXmlElement *pnode)
{
    if (pnode->Attribute("action"))
    {
        if (string(pnode->Attribute("action")) == "view_camera")
            action = TypeActionCamera;
    }

    switch (action)
    {
    case TypeActionCamera:
        if (pnode->Attribute("camera"))
            cameraId = pnode->Attribute("camera");
        break;
    default:
        break;
    }

    return true;
}

bool ActionTouchscreen::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *action_node = new TiXmlElement("calaos:action");
    action_node->SetAttribute("type", "touchscreen");
    switch (action)
    {
    case TypeActionCamera:
        action_node->SetAttribute("action", "view_camera");
        action_node->SetAttribute("camera", cameraId);
        break;
    default:
        cWarningDom("rule.action.touchscreen") << "Unknown action type!";
        break;
    }
    node->LinkEndChild(action_node);

    return true;
}
