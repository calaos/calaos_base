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
#ifndef S_CONDITIONSCRIPT_H
#define S_CONDITIONSCRIPT_H

#include "Calaos.h"
#include "Condition.h"
#include "ScriptExec.h"
#include "IOBase.h"

using namespace std;

namespace Calaos
{

class ConditionScript: public Condition
{
private:
    string script;

    /* These are declared inputs that will trigger the rule execution.
     * Most generaly, inputs are those used in the script.
     *
     * E4.2c: ids, not IOBase*. Besides removing the dangling-pointer class,
     * this makes SaveToXml() deterministic: the previous
     * unordered_map<IOBase*, IOBase*> serialized the inputs in POINTER HASH
     * order, i.e. potentially a different order at every run. The vector keeps
     * the document order of the config (duplicates dropped on insert, as the
     * map did). */
    std::vector<std::string> inEventIds;

public:
    ConditionScript();
    virtual ~ConditionScript();

    virtual bool Evaluate();
    void EvaluateAsync(std::function<void(bool eval)> cb, string triggerId);

    virtual bool LoadFromXml(TiXmlElement *node) override;
    virtual bool SaveToXml(TiXmlElement *node) override;

    //Declares `io`/`id` as a trigger of this condition. A null IO or an empty
    //id is refused and logged; adding the same id twice is a no-op.
    void addTriggerIO(IOBase *io);
    void addTriggerId(const std::string &id);

    /* Does this condition list `io` (resp. `id`) among its triggers?
     * Both work on ids, so neither resolves nor dereferences anything. */
    bool containsTriggerIO(IOBase *io);
    bool containsTriggerId(const std::string &id) const;

    int getTriggerCount() const { return inEventIds.size(); }
    const std::string &getTriggerId(int i) const;

};

}
#endif
