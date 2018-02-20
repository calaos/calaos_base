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
#include "RulesFactory.h"

using namespace Calaos;

Condition *RulesFactory::CreateCondition(TiXmlElement *node)
{
    Condition *condition = NULL;

    /* read type */
    string type = "";
    if (node->Attribute("type"))
        type = node->Attribute("type");

    /* Standard condition */
    if (type == "standard" || type == "")
    {
        condition = new ConditionStd();
    }

    /* Start condition */
    else if (type == "start")
    {
        condition = new ConditionStart();
    }

    /* Script condition */
    else if (type == "script")
    {
        condition = new ConditionScript();
    }

    /* Standard output condition */
    else if (type == "output")
    {
        condition = new ConditionOutput();
    }

    if (condition && !condition->LoadFromXml(node))
        return NULL;

    return condition;
}


Action *RulesFactory::CreateAction(TiXmlElement *node)
{
    Action *action = NULL;

    /* read type */
    string type = "";
    if (node->Attribute("type"))
        type = node->Attribute("type");

    /* Standard action */
    if (type == "standard" || type == "")
    {
        action = new ActionStd();
    }

    /* Mail action */
    else if (type == "mail")
    {
        action = new ActionMail();
    }

    /* Script action */
    else if (type == "script")
    {
        action = new ActionScript();
    }

    /* Touchscreen action */
    else if (type == "touchscreen")
    {
        action = new ActionTouchscreen();
    }

    /* Push notification action */
    else if (type == "push")
    {
        action = new ActionPush();
    }

    if (action && !action->LoadFromXml(node))
        return NULL;

    return action;
}
