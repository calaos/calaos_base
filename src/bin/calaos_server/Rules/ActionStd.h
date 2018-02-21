/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef S_ACTIONSTD_H
#define S_ACTIONSTD_H

#include "Calaos.h"
#include "Action.h"
#include "IOBase.h"

namespace Calaos
{

class ActionStd: public Action
{
protected:
    std::vector<IOBase *> outputs;
    Params params;

    //this is used to do the action
    //based on another output state
    Params params_var;

public:
    ActionStd(): Action(ACTION_STD)
    { cDebugDom("rule.action.standard") <<  "New standard action"; }
    ~ActionStd();

    void Add(IOBase *p);
    bool Execute();
    void Remove(int i);
    void Assign(int i, IOBase *obj);

    IOBase *get_output(int i) { return outputs[i]; }
    Params &get_params() { return params; }
    void set_param(Params &p) { params = p; }
    Params &get_params_var() { return params_var; }
    void set_param_var(Params &p) { params_var = p; }

    int get_size() { return outputs.size(); }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
