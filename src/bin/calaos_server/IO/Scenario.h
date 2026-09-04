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
#ifndef S_Scenario_H
#define S_Scenario_H

#include "Calaos.h"
#include "IOBase.h"

namespace Calaos
{

class AutoScenario;
/* FORWARD DECLARED, and held by pointer, on purpose: including
 * AutoScenarioDef.h here would put src/bin/calaos_server/Scenario on the
 * include path of every one of the ~65 test binaries that compile against
 * Scenario.h. Only IO/Scenario.cpp needs the definition.
 */
class AutoScenarioDef;

class Scenario : public IOBase
{
protected:
    bool value;

    AutoScenario *auto_scenario;

    /* THE DEFINITION, and it is the datum.
     *
     * Owned, never null. Loaded from the `autoscenario_*` / `as_*` params of
     * this IO when they are there, and written back into them at every
     * SaveToXml(), which is how io.xml stops being empty of the model.
     */
    AutoScenarioDef *auto_scenario_def;

    /* Refresh the definition from the rules the AutoScenario currently holds.
     * A no-op when this IO is not an auto scenario, and a no-op as soon as the
     * definition holds anything of its own.
     * Since the marker IS the uid (T3.61) an auto scenario always has a loaded
     * definition, so both guards hold and this bootstrap no longer runs; it is
     * kept because it is the only thing that could ever rebuild a definition
     * from a projection, and removing it is a decision of its own.
     *
     * It reads the action ids through ActionStd::get_output_id(), NOT through
     * get_output(): an id that no longer resolves is KEPT (D4). The pointer
     * based accessors of AutoScenario (getStepAction() & co.) drop it, because
     * isScenarioInternalIO(nullptr) answers true.
     */
    void captureDefinitionFromRules();

public:
    Scenario(Params &p);
    ~Scenario();

    virtual DATA_TYPE get_type() override { return TBOOL; }

    virtual bool get_value_bool() override { return value; }
    virtual bool set_value(bool val) override;

    AutoScenario *getAutoScenario() { return auto_scenario; }

    //The definition. Never null.
    AutoScenarioDef *getDefinition() { return auto_scenario_def; }

    /* Materializes the definition into this IO's params before the generic
     * writer serializes them, so io.xml carries the whole model.
     * Writes and removes NOTHING when this IO carries no definition.
     */
    virtual bool SaveToXml(pugi::xml_node node) override;

    virtual bool get_command_bool() override { return value; }

    /* The API payload, rendered from the DEFINITION and never from the rules:
     * an action whose IO no longer resolves comes out like any other, with
     * resolved="false". Nothing here mutates.
     * PITFALL: an action value is arbitrary client text and may carry a zero
     * byte. It goes in as a std::string, so the whole value reaches the dump
     * and is escaped there; through a const char * it would be cut at that
     * byte and answered 200 OK on an amputated value.
     */
    Json toJson();
};

}
#endif
