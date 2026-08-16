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
    /* E4.2c: the outputs are referenced BY ID, never by IOBase*. Same reasons
     * as ConditionStd: an id that stopped resolving is a nullptr handled once,
     * and the stored id is also the key of `params`/`params_var`, which the
     * pre-E4.2c code recomputed with get_param("id") on every access. */
    std::vector<std::string> outputIds;
    Params params;

    //this is used to do the action
    //based on another output state
    Params params_var;

public:
    ActionStd(): Action(ACTION_STD)
    { cDebugDom("rule.action.standard") <<  "New standard action"; }
    ~ActionStd();

    //Stores p's id, after checking p is an output. Null/id-less p is refused.
    void Add(IOBase *p);
    //Same, when only the id is known (no resolution, hence no isOutput check)
    void Add(const std::string &id);

    /* MISSING IO CONTRACT (E4.2c): an output whose id no longer resolves - or
     * resolves to something that is not an output any more - is SKIPPED, logged
     * as an error, and makes Execute() return false. The other outputs of the
     * action still run: that is the existing per-output accumulation of `ret`,
     * a partial failure has always been reported that way. */
    bool Execute();
    void Remove(int i);
    void Assign(int i, IOBase *obj);
    void Assign(int i, const std::string &id);

    /* Resolves the id through ListeRoom. NON-OWNING, **nullptr when the id is
     * unknown or out of range** - every caller must test it. */
    IOBase *get_output(int i);

    //The stored id, for the call sites that only compare ids (no resolution)
    const std::string &get_output_id(int i) const;

    Params &get_params() { return params; }
    void set_param(Params &p) { params = p; }
    Params &get_params_var() { return params_var; }
    void set_param_var(Params &p) { params_var = p; }

    int get_size() { return outputIds.size(); }

    bool LoadFromXml(TiXmlElement *node) override;
    bool SaveToXml(TiXmlElement *node) override;
};

}
#endif
