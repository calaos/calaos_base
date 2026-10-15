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

#include <cstdint>
#include <optional>

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

    //Seconds a linked sensor may stay silent, without a sensor_timeout param.
    static constexpr double DEFAULT_SENSOR_TIMEOUT = 9000;
    double sensorTimeout() const { return sensorTimeoutS; }

protected:
    ClimateBase(Params &p);

    /* Documents the command fields in IODoc (spec §11). An impl calls it from
     * its constructor with the modes it can offer and the link its mode is
     * written through, empty when the device stores the mode itself. modeNote
     * is added to the description of the mode field, modeRequires names the
     * links one mode needs on top. Fields and params only some impls honour
     * are documented by the two calls below, so that an impl never lists
     * what it ignores. */
    void documentCommand(const vector<ClimateMode> &modes, const string &modeLink,
                         const string &modeNote = string(),
                         const IODoc::ValueLinks &modeRequires = IODoc::ValueLinks());
    //The "modes" param, read by the default supportedModes().
    void documentModesParam();
    //fan_modes and swing_modes, and the fan_mode and swing_mode fields.
    void documentFanSwing();

    //The current mode; false when it is not known or not a climate mode.
    virtual bool currentMode(ClimateMode &mode);
    virtual bool writeMode(ClimateMode mode);

    //A comma separated list param, trimmed, without empty or repeated items.
    vector<string> paramList(const string &name) const;
    //Logs, at load, what the modes param holds that cannot be selected.
    virtual void checkModesParam();

    bool applyCommand(const Json &command) override;
    bool applyCommandField(const string &field, const Json &value, string &reason) override;

    //The IO a role is written to: its command_io, else its io.
    IOBase *writeTarget(const string &role);

    /* Whether a command to this role can reach the appliance. A role that
     * cannot is neither offered as a capability nor applied: writing it
     * would report success and send nothing. */
    virtual bool isWritable(const string &role);

    /* The value a range bound (target_temp_low/_high) is validated against
     * when a command moves only the other one; false while it is unknown.
     * An impl whose readback lags its commands overrides both hooks. */
    virtual bool rangeBound(const string &role, double &value);
    virtual bool writeRangeBound(const string &role, double value);

    enum class Freshness
    {
        Fresh,
        //No reading since the server started: waited for, not stale.
        NotRead,
        Silent,
        Disconnected
    };
    /* Fresh: `io` has delivered a reading since the server started, the last
     * one less than sensor_timeout ago, and does not report itself
     * disconnected. */
    Freshness freshness(IOBase *io) const;
    bool isFresh(IOBase *io) const { return freshness(io) == Freshness::Fresh; }
    //"has sent nothing for <sensor_timeout> s" or "reports itself disconnected".
    string staleReason(Freshness f) const;
    //Logged at load: a sensor that does not date every reading looks silent while steady.
    void warnIfChangesOnly(const string &role);
    /* A sensor that sends on change only stays silent in a steady room: past
     * half of sensor_timeout without a reading, it is asked for one. */
    void requestReadingIfOld(const string &role);

    Json deviceCapabilities() override;
    Json deviceState() override;

    //The param as a number >= 0; `def` when absent, and logged when invalid.
    double nonNegativeParam(const char *name, double def);

    //Seconds to ms, capped at about 30 years: beyond, llround() and the deadlines would overflow.
    static uint64_t toMs(double seconds);
    //The loop clock the timers use, refreshed: outside a loop turn it lags.
    static uint64_t loopNow();

    struct Lockouts
    {
        uint64_t minOnMs = 0;
        uint64_t minOffMs = 0;
        //After the other output went OFF, before this one may start: a changeover.
        uint64_t changeoverMs = 0;
    };
    /* ms before an output may switch to `on`, from the loop times of its last
     * switches and of the other output's last OFF. A forced stop waits for
     * nothing; min_off_time still guards the restart. */
    static uint64_t lockoutWait(bool on, bool forced, const std::optional<uint64_t> &onAt,
                                const std::optional<uint64_t> &offAt,
                                const std::optional<uint64_t> &otherOffAt, const Lockouts &l,
                                uint64_t now);

private:
    //Both bounds linked and writable.
    bool rangeAvailable();
    //min of the low IO, max of the high IO.
    Bounds rangeBounds();
    bool applyRange(const Json &command);

    double sensorTimeoutS = DEFAULT_SENSOR_TIMEOUT;
};

} //namespace Calaos

#endif
