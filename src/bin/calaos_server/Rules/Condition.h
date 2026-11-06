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
#ifndef S_CONDITION_H
#define S_CONDITION_H

#include <algorithm>
#include <string>
#include <vector>

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

    /* -------------------------------------------------------------------
     * Load-time diagnosis (E4.2e)
     *
     * Ids this condition references and that did NOT resolve when the config
     * was read. This is how the "IO not found" cause is told apart from every
     * other reason a node can be refused:
     *
     *   - a malformed/unknown node        -> LoadFromXml() returns false, the
     *                                        factory returns NULL, the caller
     *                                        drops the condition (unchanged),
     *   - an id that does not resolve     -> LoadFromXml() returns TRUE, the
     *                                        reference and its parameters are
     *                                        kept as they stand, and the id is
     *                                        recorded here.
     *
     * Keeping the object is what makes the save non destructive: the id, the
     * operator and the value are still there, so SaveToXml() writes them back
     * verbatim instead of dropping the user's condition on the first save.
     * Rule::AddCondition() reads this and disables the whole rule (see Rule.h):
     * an amputated rule is MORE permissive than the one the user wrote.
     *
     * It is a LOAD-time record, never updated at evaluation: an IO removed
     * while the server runs is handled by ListeRule::RemoveRule() (the rule is
     * deleted) and, failing that, by the fail-closed Evaluate() of E4.2c.
     * ---------------------------------------------------------------- */
    vector<string> missingIoIds;

    void addMissingIo(const string &id)
    {
        string key = id.empty()? MISSING_IO_EMPTY: id;

        if (std::find(missingIoIds.begin(), missingIoIds.end(), key) != missingIoIds.end())
            return;
        missingIoIds.push_back(key);
    }

public:
    /* An EMPTY id is a missing dependency too, and it is recorded under this
     * sentinel instead of being skipped. Skipping it left the condition with
     * ZERO input and NO flag - and ConditionStd::Evaluate() answers true for
     * zero input, which is exactly the "an amputated conjunction is a more
     * permissive rule" hole this ticket exists to close. It is reachable from
     * a hand-written rules.xml with `id=""` or with no `id` attribute at all.
     * There is no legitimate id-less reference here: ConditionStd and
     * ConditionOutput always name an IO, and ConditionStart - the only
     * condition naming none - never records anything. */
    static constexpr const char *MISSING_IO_EMPTY = "<no id>";

    Condition(int type);
    virtual ~Condition();

    virtual bool Evaluate();

    int getType() { return condition_type; }

    //True when at least one id of this condition did not resolve at load time
    bool hasMissingIo() const { return !missingIoIds.empty(); }
    const vector<string> &getMissingIoIds() const { return missingIoIds; }

    virtual bool LoadFromXml(pugi::xml_node node) { return true; }
    virtual bool SaveToXml(pugi::xml_node node) { return true; }
};

}
#endif
