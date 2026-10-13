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
#ifndef S_INTERNALTHERMOSTAT_H
#define S_INTERNALTHERMOSTAT_H

#include <cstdint>
#include <memory>
#include <optional>

#include "ClimateBase.h"

namespace Calaos
{

/* impl="thermostat": Calaos regulates (spec §8). output_heat and output_cool
 * follow current_temp and the setpoints; the mode is stored by the device and
 * persisted in the state cache. Deleting the device leaves the outputs as
 * they are: today that only happens on a configuration reload or a server
 * stop, when the outputs and their drivers may already be gone. */
class InternalThermostat: public ClimateBase
{
public:
    InternalThermostat(Params &p);

    void resolveLinks() override;

    //off, plus heat with output_heat, cool with output_cool, auto with both.
    vector<ClimateMode> supportedModes() override;

    static constexpr double DEFAULT_HYSTERESIS = 0.5;
    //Values of the setpoint IOs recreated when the file lost them.
    static constexpr double DEFAULT_TARGET = 20;
    static constexpr double DEFAULT_LOW = 19;
    static constexpr double DEFAULT_HIGH = 25;

protected:
    struct OutputPair
    {
        bool heat = false;
        bool cool = false;
    };

    /* Asked first at each evaluation: an output refused here is switched
     * OFF at once, whatever its min_on_time; its min_off_time still guards
     * the restart. Both are refused until current_temp holds a value. */
    virtual OutputPair outputsAllowed();

    /* Spec §8.1. Never nested: when an output write runs rules that call it
     * again, the current pass stops and starts over once it returns. */
    void evaluate();

    bool currentMode(ClimateMode &mode) override;
    bool writeMode(ClimateMode mode) override;
    bool applyCommand(const Json &command) override;

private:
    struct Output
    {
        const char *role;
        //The last state written; unknown until the first evaluation.
        std::optional<bool> commanded;
        //Loop time (ms) of the last switches, the start of the lock-outs.
        std::optional<uint64_t> onAt;
        std::optional<uint64_t> offAt;
        //Re-runs evaluate() once a deferred switch is allowed.
        std::unique_ptr<Timer> lockout;
    };

    double nonNegativeParam(const char *name, double def);
    void recreateOwnedIos();
    void restoreMode();

    void regulate();
    bool demand(Output &out, double temp, const char *setpoint, bool cooling);
    //The state the thermostat holds: its last write, else what the IO reports.
    bool held(Output &out);
    //ON for the other output: as last written or as read back.
    bool isOn(Output &out);
    void switchOutput(Output &out, Output &other, bool on, uint64_t now, bool forced);
    uint64_t lockedFor(const Output &out, const Output &other, bool on, uint64_t now,
                       bool forced) const;
    void writeAction();

    ClimateMode mode = ClimateMode::Off;
    double hysteresis = DEFAULT_HYSTERESIS;
    uint64_t minOnMs = 0;
    uint64_t minOffMs = 0;

    Output heat{ "output_heat" };
    Output cool{ "output_cool" };

    bool evaluating = false;
    bool evaluateAgain = false;

    vector<IOChangeHub::Subscription> triggers;
    std::unique_ptr<Timer> outputsMoved;
};

} //namespace Calaos

#endif
