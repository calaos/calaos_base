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
#ifndef S_CONDITIONSCRIPT_H
#define S_CONDITIONSCRIPT_H

#include "Calaos.h"
#include "Condition.h"
#include "ScriptManager.h"
#include "IOBase.h"

using namespace std;

namespace Calaos
{

class ConditionScript: public Condition
{
private:
    string script;

    //These are declared inputs that will trigger the rule execution
    //Most generaly, inputs are those used in the script
    unordered_map<IOBase *, IOBase *> in_event;

public:
    ConditionScript();
    virtual ~ConditionScript();

    virtual bool Evaluate();
    void EvaluateAsync(std::function<void(bool eval)> cb);

    virtual bool LoadFromXml(TiXmlElement *node);
    virtual bool SaveToXml(TiXmlElement *node);

    bool containsTriggerIO(IOBase *io);

};

}
#endif
