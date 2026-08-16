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

bool ActionTouchscreen::LoadFromXml(pugi::xml_node pnode)
{
    if (pnode.attribute("action"))
    {
        if (string(pnode.attribute("action").as_string()) == "view_camera")
            action = TypeActionCamera;
    }

    switch (action)
    {
    case TypeActionCamera:
        if (pnode.attribute("camera"))
            cameraId = pnode.attribute("camera").as_string();
        break;
    default:
        break;
    }

    return true;
}

bool ActionTouchscreen::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node action_node = node.append_child("calaos:action");
    XmlUtils::setAttribute(action_node, "type", "touchscreen");
    switch (action)
    {
    case TypeActionCamera:
        XmlUtils::setAttribute(action_node, "action", "view_camera");
        XmlUtils::setAttribute(action_node, "camera", cameraId);
        break;
    default:
        cWarningDom("rule.action.touchscreen") << "Unknown action type!";
        break;
    }

    return true;
}
