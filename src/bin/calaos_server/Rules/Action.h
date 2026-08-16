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
#ifndef S_ACTION_H
#define S_ACTION_H

#include <algorithm>
#include <string>
#include <vector>

#include "Calaos.h"

using namespace std;

namespace Calaos
{
//-----------------------------------------------------------------------------
//      Base class for all actions
//-----------------------------------------------------------------------------

enum
{
    ACTION_UNKONWN = 0,
    ACTION_STD,
    ACTION_MAIL,
    ACTION_SCRIPT,
    ACTION_TOUCHSCREEN,
    ACTION_PUSH,
};

class Action
{
protected:
    int action_type;

    /* Load-time diagnosis (E4.2e). Same contract as Condition::missingIoIds,
     * see the long comment in Condition.h: an id that does not resolve at load
     * is RECORDED here and KEPT (with its value) so that the save stays non
     * destructive, while a malformed node still makes LoadFromXml() return
     * false and the action is dropped as before. Rule::AddAction() reads this
     * and disables the whole rule. */
    vector<string> missingIoIds;

    void addMissingIo(const string &id)
    {
        string key = id.empty()? MISSING_IO_EMPTY: id;

        if (std::find(missingIoIds.begin(), missingIoIds.end(), key) != missingIoIds.end())
            return;
        missingIoIds.push_back(key);
    }

public:
    /* Same sentinel as Condition::MISSING_IO_EMPTY, see the long comment
     * there: an empty/absent id is a missing dependency, not something to
     * skip. Skipping it left the action with ZERO output and NO flag, so the
     * rule stayed enabled while doing less than what the user wrote. */
    static constexpr const char *MISSING_IO_EMPTY = "<no id>";

    Action(int type);
    virtual ~Action();

    virtual bool Execute();

    int getType() { return action_type; }

    //True when at least one id of this action did not resolve at load time
    bool hasMissingIo() const { return !missingIoIds.empty(); }
    const vector<string> &getMissingIoIds() const { return missingIoIds; }

    virtual bool LoadFromXml(pugi::xml_node node) { return true; }
    virtual bool SaveToXml(pugi::xml_node node) { return true; }
};

}
#endif
