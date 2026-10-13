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
#ifndef S_CLIMATEBASE_H
#define S_CLIMATEBASE_H

#include "Device.h"

namespace Calaos
{

//Serialized as the ESPHome strings: off, heat, cool, auto, dry, fan_only.
enum class ClimateMode
{
    Off,
    Heat,
    Cool,
    Auto,
    Dry,
    FanOnly
};
//off, idle, heating, cooling, drying, fan.
enum class ClimateAction
{
    Off,
    Idle,
    Heating,
    Cooling,
    Drying,
    Fan
};

/* The climate family (spec §6). What an instance can do follows from which
 * links resolve, plus the fan_modes/swing_modes/modes params; setpoint bounds
 * come from the linked IO. No behaviour of its own: impls derive from it. */
class ClimateBase: public Device
{
public:
    struct Bounds
    {
        double min;
        double max;
        double step;
        string unit;
    };

    //Used when the setpoint IO does not set value_min/value_max/step/unit.
    static constexpr double DEFAULT_TEMP_MIN = 7;
    static constexpr double DEFAULT_TEMP_MAX = 35;
    static constexpr double DEFAULT_TEMP_STEP = 0.5;
    static constexpr const char *DEFAULT_TEMP_UNIT = "°C";

    static const vector<ClimateMode> &allModes();
    static string modeName(ClimateMode mode);
    static bool parseMode(const string &name, ClimateMode &mode);
    static string actionName(ClimateAction action);
    static bool parseAction(const string &name, ClimateAction &action);

    void resolveLinks() override;

    /* The modes a command may select, in enum order. By default those of
     * the "modes" param (all of them without it) when the "mode" link
     * resolves, none otherwise. An impl that owns its mode, or whose
     * outputs limit it, overrides this. */
    virtual vector<ClimateMode> supportedModes();

    //Bounds of the IO a setpoint role writes: its command_io, else its io.
    Bounds setpointBounds(const string &role);

    //The "min_range" param; 0 when absent or not a non-negative number.
    double minRange() const;

protected:
    ClimateBase(Params &p);

    //The current mode; false when it is not known or not a climate mode.
    virtual bool currentMode(ClimateMode &mode);
    virtual bool writeMode(ClimateMode mode);

    //A comma separated list param, trimmed, without empty or repeated items.
    vector<string> paramList(const string &name) const;

    bool applyCommand(const Json &command) override;
    bool applyCommandField(const string &field, const Json &value, string &reason) override;

    Json deviceCapabilities() override;
    Json deviceState() override;

private:
    IOBase *writeTarget(const string &role);
    bool applyRange(const Json &command);
};

} //namespace Calaos

#endif
