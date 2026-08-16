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
    /* E4.2c: the output is referenced BY ID, never by IOBase*.
     * The raw pointer it replaces was also left UNINITIALIZED by the
     * constructor and dereferenced unguarded by Evaluate()/SaveToXml(): an
     * unset output is now just an empty (unresolvable) id. */
    string outputId;
    string params;
    string ops;
    //this is used to do the condition test
    //based on another output
    string params_var;

    bool trigger = true;

    bool eval(bool val1, std::string oper, bool val2);
    bool eval(double val1, std::string oper, double val2);
    bool eval(std::string val1, std::string oper, std::string val2);
    bool eval(IOBase *out, std::string oper, std::string val);

public:
    ConditionOutput();
    ~ConditionOutput();

    /* MISSING IO CONTRACT (E4.2c): an output whose id no longer resolves makes
     * the condition FALSE, logged as an error. Same rule as ConditionStd. */
    virtual bool Evaluate();

    //Stores p's id (a null p, or an IO without an id, clears the reference)
    void setOutput(IOBase *p);
    void setOutputId(const std::string &id) { outputId = id; }

    /* Resolves through ListeRoom. NON-OWNING, **nullptr when the id is unknown
     * or was never set** - callers must test it. */
    IOBase *getOutput();

    //The stored id, for the call sites that only compare ids (no resolution)
    const std::string &getOutputId() const { return outputId; }

    bool useForTrigger() { return trigger; }

    string get_params() { return params; }
    string get_operator() { return ops; }
    string get_params_var() { return params_var; }
    void set_param(string p) { params = p; }
    void set_operator(string p) { ops = p; }
    void set_param_var(string p) { params_var = p; }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
