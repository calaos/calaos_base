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
#include <map>
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
    ~InternalThermostat();

    void resolveLinks() override;

    /* Spec §8.3, the observer registered with Device::observeOutsideWrites():
     * warns when `io` is an output of a thermostat. It never refuses: the
     * write goes on and the thermostat takes the output back by its own
     * rules. */
    static void warnIfProtected(IOBase &io, const char *source);

    /* off, plus heat with output_heat, cool with output_cool, auto with both.
     * A floor (emitter=floor) cools only with a humidity link. */
    vector<ClimateMode> supportedModes() override;

    static constexpr double DEFAULT_HYSTERESIS = 0.5;
    //Relative humidity (%) from which floor cooling stops.
    static constexpr double DEFAULT_HUMIDITY_MAX = 65;
    //Seconds between two put-backs of an output something else keeps switching.
    static constexpr double DEFAULT_PUT_BACK_DELAY = 1;
    static constexpr double DEFAULT_PUT_BACK_DELAY_MAX = 60;
    static constexpr double MIN_PUT_BACK_DELAY = 0.001;
    //Longest wait before a silent sensor is seen stale, a repeated reading fresh,
    //or an old sensor asked for a reading.
    static constexpr double FRESHNESS_POLL = 60;
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
     * the restart. The base refuses floor cooling (emitter=floor) unless
     * humidity is fresh and below humidity_max. Whatever an override says,
     * both stay OFF while current_temp is not fresh (sensor_timeout). */
    virtual OutputPair outputsAllowed();

    /* Spec §8.1. Never nested: when an output write runs rules that call it
     * again, the current pass stops and starts over once it returns. */
    void evaluate();

    //The range serves auto only: not on a floor that cannot cool.
    bool isWritable(const string &role) override;
    bool currentMode(ClimateMode &mode) override;
    bool writeMode(ClimateMode mode) override;
    //Adds lockout and fault (spec §5.3).
    Json deviceState() override;
    bool applyCommand(const Json &command) override;
    Json deviceIos() override;

private:
    struct Output
    {
        const char *role;

        //Copied whole, to be undone when the IO refuses a write.
        struct State
        {
            /* The last state written successfully, else what something else
             * switched it to, else the first one read back. */
            std::optional<bool> commanded;
            //Loop time (ms) of the last switches, the start of the lock-outs.
            std::optional<uint64_t> onAt;
            std::optional<uint64_t> offAt;
            //commanded was set by something else: switching back is a put-back.
            bool foreign = false;
            //The last put-back was undone within its spacing.
            bool fighting = false;
            std::optional<uint64_t> putBackAt;
            uint64_t spacingMs = 0;
            //The last write of the thermostat was a put-back.
            bool putBackWritten = false;
        } st;

        bool warned = false;
        //Held OFF by the mode or by outputsAllowed(), whatever the demand.
        bool stopForced = false;
        //Last warning per "<source>", see warnIfProtected().
        std::map<std::string, uint64_t> writeWarnedAt;
        //Already the output of another thermostat, or both outputs at once.
        bool refused = false;
        //Re-runs evaluate() once a deferred switch is allowed.
        std::unique_ptr<Timer> lockout;
        //Loop time (ms) at which a wanted ON waiting for its lock-out may start.
        std::optional<uint64_t> startsAt;
    };

    double nonNegativeParam(const char *name, double def);
    void readPutBackDelays();
    void readValidBounds();
    void claimOutputs();
    //The IO of an output link, nullptr when missing or refused.
    IOBase *outputIo(Output &out);
    void recreateOwnedIos();
    //output_cool linked, and humidity too on a floor.
    bool canCool();
    void restoreMode();

    void evaluateSoon();
    void noticeOutput(Output &out, IOBase &io);
    void warnWrite(Output &out, IOBase &io, const char *source);
    //current_temp fresh and within the valid bounds, its silence or excess logged once.
    bool sensorFresh(IOBase *sensor);
    //Floor cooling: humidity fresh and below humidity_max, changes logged.
    bool humidityAllowsCooling();
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
    //The safety rule holding the outputs off, "" when none: the state's fault.
    string safetyFault(bool cooling);
    //Publishes the device when its fault or its lock-out starts or ends.
    void publishStatusChange();
    //The output whose wanted ON waits for its lock-out, nullptr when none.
    const Output *waitingOutput() const;

    ClimateMode mode = ClimateMode::Off;
    double hysteresis = DEFAULT_HYSTERESIS;
    uint64_t minOnMs = 0;
    uint64_t minOffMs = 0;
    uint64_t putBackMs = 0;
    uint64_t putBackMaxMs = 0;

    Output heat{ "output_heat" };
    Output cool{ "output_cool" };

    //Logged when it changes: once per silence or disconnection.
    Freshness sensorState = Freshness::NotRead;
    //valid_min / valid_max of current_temp; absent = unchecked.
    std::optional<double> validMin;
    std::optional<double> validMax;
    //The last fresh reading was outside the bounds.
    bool outOfRange = false;
    //Its excess was logged: once per episode, not while the mode is off.
    bool outOfRangeLogged = false;
    string fault;
    //What was last published of the fault and lock-out, to publish their changes.
    string publishedStatus;

    //emitter=floor: cooling needs a humidity below humidity_max.
    bool floorCooling = false;
    double humidityMax = DEFAULT_HUMIDITY_MAX;
    enum class HumidityCut
    {
        None,
        Unknown,
        Stale,
        High
    } humidityCut = HumidityCut::None;
    //Outputs claimed: a later thermostat must not drive them.
    bool claimed = false;
    bool evaluating = false;
    bool evaluateAgain = false;

    vector<IOChangeHub::Subscription> triggers;
    //One evaluation on the next loop turn after an output moved.
    std::unique_ptr<Timer> outputsMoved;
    /* A sensor going silent signals nothing, and a driver drops a repeated
     * reading as unchanged: only time tells either. */
    std::unique_ptr<Timer> freshnessCheck;
};

} //namespace Calaos

#endif
