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
#include <Action.h>

using namespace Calaos;

Action::Action(int type): action_type(type)
{
        Utils::logger("rule.action") << Priority::DEBUG << "Action::Action(): New action" << log4cpp::eol;
}

Action::~Action()
{
        Utils::logger("rule.action") << Priority::DEBUG << "Action::~Action(): Ok" << log4cpp::eol;
}

bool Action::Execute()
{
        Utils::logger("rule.action") << Priority::ERROR << "Action::Execute(): Can't execute base Action class !" << log4cpp::eol;

        return false;
}
