/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_CONDITION_H
#define S_CONDITION_H

#include "Calaos.h"

using namespace std;

namespace Calaos
{
//-----------------------------------------------------------------------------
//      Base class for all conditions
//-----------------------------------------------------------------------------

enum { COND_UNKONWN = 0, COND_STD, COND_START, COND_SCRIPT, COND_OUTPUT };

class Condition
{
protected:
    int condition_type;

public:
    Condition(int type);
    virtual ~Condition();

    virtual bool Evaluate();

    int getType() { return condition_type; }

    virtual bool LoadFromXml(TiXmlElement *node) { return true; }
    virtual bool SaveToXml(TiXmlElement *node) { return true; }
};

}
#endif
