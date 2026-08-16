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

//Operator-evaluation helper shared by ConditionStd and ConditionOutput (their
//eval(bool/double/string) overloads were duplicated otherwise). Defined once
//in ConditionStd.cpp, used by both .cpp files. logDomain is only needed by
//the overloads that can reject an unknown operator.
namespace ConditionEval
{
    bool evalOperator(bool val1, std::string oper, bool val2, const char *logDomain);
    bool evalOperator(double val1, std::string oper, double val2);
    bool evalOperator(std::string val1, std::string oper, std::string val2, const char *logDomain);
}

class ConditionStd: public Condition
{
protected:
    /* E4.2c: the inputs are referenced BY ID, never by IOBase*.
     *
     * A stored pointer used to dangle as soon as the IO was destroyed by any
     * path that did not go through ListeRule::RemoveRule() (an IO deleted with
     * `modify` set, a Room dying, a reload). An id that no longer resolves is
     * a plain nullptr from ListeRoom::findIO(), handled once in Evaluate().
     *
     * The id stored is the *resolved* IO's own id, which is exactly the key
     * `params`/`ops`/`params_var` are keyed on and the one the pre-E4.2c code
     * recomputed as `inputs[i]->get_param("id")` at every use. It is therefore
     * also cheaper: one std::string member instead of a map lookup + a string
     * copy per access, several times per input per evaluation.
     */
    std::vector<std::string> inputIds;
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

    /* MISSING IO CONTRACT (E4.2c): an input whose id no longer resolves makes
     * the condition FALSE (fail closed) and is logged as an error. It is never
     * skipped silently: a condition is a conjunction, and dropping a term would
     * let the rule fire on a config that no longer describes what it referred
     * to. A miss is never dereferenced. */
    virtual bool Evaluate();

    /* Stores p's id. A null p, or an IO without an id, is refused and logged:
     * both used to be pushed and dereferenced right away. */
    void Add(IOBase *p);
    //Same, when only the id is known (no resolution is attempted here)
    void Add(const std::string &id);
    void Remove(int i);
    void Assign(int i, IOBase *obj);
    void Assign(int i, const std::string &id);

    void getVarIds(vector<IOBase *> &list);
    /* Ids of the `val_var` targets, without resolving them. This is what the
     * trigger dispatch needs: it compares ids, so it must not pay a resolution
     * (nor lose an unresolvable entry) to do it. */
    void getVarIds(vector<std::string> &list);
    bool useForTrigger() { return trigger; }

    /* Resolves the id through ListeRoom. NON-OWNING, and **nullptr when the id
     * is unknown or out of range** - every caller must test it. */
    IOBase *get_input(int i);

    /* The stored id itself, for the call sites that only compare ids (rule
     * dispatch, rule removal): no resolution, no dereference. Out of range
     * yields an empty string. */
    const std::string &get_input_id(int i) const;
    Params &get_params() { return params; }
    Params &get_operator() { return ops; }
    Params &get_params_var() { return params_var; }
    void set_param(Params &p) { params = p; }
    void set_operator(Params &p) { ops = p; }
    void set_param_var(Params &p) { params_var = p; }

    int get_size() { return inputIds.size(); }

    bool LoadFromXml(TiXmlElement *node);
    bool SaveToXml(TiXmlElement *node);
};

}
#endif
