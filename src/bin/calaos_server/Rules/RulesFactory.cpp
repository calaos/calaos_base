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
#include "RulesFactory.h"

using namespace Calaos;

/******************************************************************************
 * The two rejection causes (E4.2e)
 * ===============================
 * A `<calaos:condition>`/`<calaos:action>` node can be refused for two very
 * different reasons, and the caller used to get the same undifferentiated NULL
 * for both:
 *
 *  1. THE NODE IS NOT USABLE - unknown `type` attribute, missing mandatory
 *     child (`<calaos:script>`, `<calaos:mail>`...). This is a corrupt/foreign
 *     config, it is dropped exactly as it always was: LoadFromXml() answers
 *     false, this factory destroys the half-built object and returns NULL, and
 *     Rule::LoadFromXml() simply does not add it. The rule stays ENABLED.
 *
 *  2. THE NODE IS FINE BUT AN IO IT NAMES DOES NOT EXIST. This is NOT a broken
 *     config, it is a config describing an IO that was deleted, renamed, or
 *     whose driver was removed. Since E4.2e the loaders keep the object with
 *     its reference and its parameters intact and only flag it
 *     (Condition/Action::hasMissingIo()), so:
 *       - the factory returns a NON-NULL object here,
 *       - Rule::AddCondition()/AddAction() see the flag and DISABLE the whole
 *         rule (an amputated rule is a more permissive rule),
 *       - SaveToXml() writes the reference back untouched, so rewriting the
 *         config does not destroy what the user wrote.
 *
 * So the distinction is carried by the returned pointer itself: NULL means
 * cause 1, non-NULL + hasMissingIo() means cause 2. No signature had to change
 * and no caller of CreateCondition()/CreateAction() had to learn a new
 * protocol to keep its old behaviour.
 ******************************************************************************/

Condition *RulesFactory::CreateCondition(pugi::xml_node node)
{
    Condition *condition = NULL;

    /* read type */
    string type = "";
    if (node.attribute("type"))
        type = node.attribute("type").as_string();

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
    {
        //Cause 1 only (see the header comment): the node itself is unusable.
        //A missing IO does NOT come here any more, it comes back as a non-null
        //condition flagged with hasMissingIo().
        delete condition;
        return NULL;
    }

    return condition;
}


Action *RulesFactory::CreateAction(pugi::xml_node node)
{
    Action *action = NULL;

    /* read type */
    string type = "";
    if (node.attribute("type"))
        type = node.attribute("type").as_string();

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
    {
        //Same as CreateCondition(): cause 1 only
        delete action;
        return NULL;
    }

    return action;
}
