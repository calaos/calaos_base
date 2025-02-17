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
#ifndef S_CONDITIONSTD_H
#define S_CONDITIONSTD_H

#include "Calaos.h"
#include "Condition.h"
#include "IOBase.h"

namespace Calaos
{

class ConditionStd: public Condition
{
protected:
    std::vector<IOBase *> inputs;
    Params params;
    Params ops;
    //this is used to do the condition test
    //based on another input
    Params params_var;
    bool trigger = true;

    bool eval(bool val1, std::string oper, bool val2);
    bool eval(double val1, std::string oper, double val2);
    bool eval(std::string val1, std::string oper, std::string val2);

public:
    ConditionStd();
    ~ConditionStd();

    virtual bool Evaluate();

    void Add(IOBase *p);
    void Remove(int i);
    void Assign(int i, IOBase *obj);

    void getVarIds(vector<IOBase *> &list);
    bool useForTrigger() { return trigger; }

    IOBase *get_input(int i) { return inputs[i]; }
    Params &get_params() { return params; }
    Params &get_operator() { return ops; }
    Params &get_params_var() { return params_var; }
    void set_param(Params &p) { params = p; }
    void set_operator(Params &p) { ops = p; }
    void set_param_var(Params &p) { params_var = p; }

    int get_size() { return inputs.size(); }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
