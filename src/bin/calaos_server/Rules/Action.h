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
#ifndef S_ACTION_H
#define S_ACTION_H

#include <Calaos.h>

using namespace std;

namespace Calaos
{
//-----------------------------------------------------------------------------
//      Base class for all actions
//-----------------------------------------------------------------------------

enum { ACTION_UNKONWN = 0, ACTION_STD, ACTION_MAIL, ACTION_SCRIPT, ACTION_TOUCHSCREEN };

class Action
{
        protected:
                int action_type;

        public:
                Action(int type);
                virtual ~Action();

                virtual bool Execute();

                int getType() { return action_type; }

                virtual bool LoadFromXml(TiXmlElement *node) { return true; }
                virtual bool SaveToXml(TiXmlElement *node) { return true; }
};

}
#endif
