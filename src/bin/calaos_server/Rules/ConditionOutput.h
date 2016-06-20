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
#ifndef S_CONDITIONOUTPUT_H
#define S_CONDITIONOUTPUT_H

#include "Calaos.h"
#include "Condition.h"
#include "IOBase.h"

namespace Calaos
{

class ConditionOutput: public Condition
{
protected:
    IOBase *output;
    std::string params;
    std::string ops;
    //this is used to do the condition test
    //based on another output
    std::string params_var;

    bool trigger = true;

    bool eval(bool val1, std::string oper, bool val2);
    bool eval(double val1, std::string oper, double val2);
    bool eval(std::string val1, std::string oper, std::string val2);
    bool eval(IOBase *out, std::string oper, std::string val);

public:
    ConditionOutput();
    ~ConditionOutput();

    virtual bool Evaluate();

    void setOutput(IOBase *p) { output = p; }
    IOBase *getOutput() { return output; }

    bool useForTrigger() { return trigger; }

    std::string get_params() { return params; }
    std::string get_operator() { return ops; }
    std::string get_params_var() { return params_var; }
    void set_param(std::string p) { params = p; }
    void set_operator(std::string p) { ops = p; }
    void set_param_var(std::string p) { params_var = p; }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
