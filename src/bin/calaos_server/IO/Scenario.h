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
#include "Jansson_Addition.h"

namespace Calaos
{

class AutoScenario;
/* E4.6b. FORWARD DECLARED, and held by pointer, on purpose: including
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

    /* E4.6b - THE DEFINITION, and it is the datum (E4.6.md D1).
     *
     * Owned, never null. Loaded from the `autoscenario_*` / `as_*` params of
     * this IO when they are there, and written back into them at every
     * SaveToXml(), which is how io.xml stops being empty of the model.
     *
     * TRANSITIONAL, AND THE SEAM E4.6c CUTS: in E4.6b the rules are still the
     * source of truth, so the definition is CAPTURED from them at save time
     * (captureDefinitionFromRules() below). E4.6c reverses the arrow -
     * rebuildRules() regenerates the rules FROM here - and the capture goes.
     */
    AutoScenarioDef *auto_scenario_def;

    /* Refresh the definition from the rules the AutoScenario currently holds.
     * A no-op when this IO is not an auto scenario.
     *
     * It reads the action ids through ActionStd::get_output_id(), NOT through
     * get_output(): an id that no longer resolves is KEPT (D4). The pointer
     * based accessors of AutoScenario (getStepAction() & co.) drop it, because
     * isScenarioInternalIO(nullptr) answers true - which is precisely the
     * amputation of RC3 and must not be reproduced here.
     *
     * The uid and the step ids already in the definition are REUSED, so two
     * consecutive saves of an unchanged scenario write the same bytes.
     */
    void captureDefinitionFromRules();

public:
    Scenario(Params &p);
    ~Scenario();

    virtual DATA_TYPE get_type() override { return TBOOL; }

    virtual bool get_value_bool() override { return value; }
    virtual bool set_value(bool val) override;

    AutoScenario *getAutoScenario() { return auto_scenario; }

    //E4.6b. The definition. Never null.
    AutoScenarioDef *getDefinition() { return auto_scenario_def; }

    /* E4.6b. Materializes the definition into this IO's params before the
     * generic writer serializes them, so io.xml carries the whole model
     * (E4.6.md D2). Writes and removes NOTHING when this IO carries no
     * definition.
     */
    virtual bool SaveToXml(pugi::xml_node node) override;

    virtual bool get_command_bool() override { return value; }

    json_t *toJson();
};

}
#endif
