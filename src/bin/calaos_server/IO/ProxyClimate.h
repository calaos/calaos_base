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
#ifndef S_PROXYCLIMATE_H
#define S_PROXYCLIMATE_H

#include <chrono>
#include <map>
#include <memory>
#include <set>

#include "ClimateBase.h"

namespace Calaos
{

/* The appliance regulates and Calaos relays (spec §7): every field is read
 * from the io of its link and written to its command_io, or to the io when
 * that is an output. Impl "proxy".
 *
 * Mode: mirrors the "mode" link. ESPHome's heat_cool reads as auto; auto is
 * written as the "auto_payload" param (heat_cool or auto), else as the
 * spelling the appliance last reported, else heat_cool.
 *
 * A link with no command_io whose io is not an output cannot be written: its
 * capability is not offered, commands to it are refused and nothing is relayed
 * to it.
 *
 * emulate_range: for a unit with a single setpoint, the device
 * keeps the low and high setpoints itself and its auto drives the unit in
 * heat, cool or off from the room temperature. The unit never receives auto. */
class ProxyClimate: public ClimateBase
{
public:
    ProxyClimate(Params &p);
    ~ProxyClimate();

    void resolveLinks() override;

    //With emulate_range, auto is offered once the unit takes heat and cool.
    vector<ClimateMode> supportedModes() override;

    //Used without the "remote_temp_keepalive" / "readback_hold" params (seconds).
    static constexpr double DEFAULT_KEEPALIVE = 120;
    static constexpr double DEFAULT_READBACK_HOLD = 10;
    //Defaults of the emulated range: degrees, then seconds.
    static constexpr double DEFAULT_HYSTERESIS = 0.5;
    static constexpr double DEFAULT_MIN_ON = 300;
    static constexpr double DEFAULT_MIN_OFF = 300;
    static constexpr double DEFAULT_CHANGEOVER = 600;
    //Longest wait before a silent room sensor is seen stale.
    static constexpr double FRESHNESS_POLL = 60;

protected:
    bool isWritable(const string &role) override;
    bool currentMode(ClimateMode &mode) override;
    bool writeMode(ClimateMode mode) override;
    //Adds connected:false while the appliance cannot be reached.
    Json deviceState() override;
    bool rangeBound(const string &role, double &value) override;
    bool writeRangeBound(const string &role, double value) override;
    //Defers the emulation until every field of the command is applied.
    bool applyCommand(const Json &command) override;
    //target_temp is refused while auto drives the unit from the range.
    bool applyCommandField(const string &field, const Json &value, string &reason) override;

private:
    using Clock = std::chrono::steady_clock;

    struct Commanded
    {
        double value;
        Clock::time_point at;
    };

    //A strictly positive number of seconds from a param, else `fallback`.
    double secondsParam(const string &name, double fallback) const;
    string linkIoId(const string &role) const;
    void startRemoteTemp();
    void relayRemoteTemp();
    void logUnknownMode(const string &raw);

    //The appliance's own spelling of auto: "auto" or "heat_cool".
    string autoReported;
    std::set<string> unknownModes;

    /* Last value commanded to a range bound, until its readback changes or
     * readback_hold runs out: two quick partial commands would otherwise
     * validate the second against the value the first replaced. */
    std::map<string, Commanded> commanded;
    double readbackHold = DEFAULT_READBACK_HOLD;
    std::vector<IOChangeHub::Subscription> readbackSubscriptions;

    std::unique_ptr<Timer> keepalive;
    IOChangeHub::Subscription sourceSubscription;
    //Logged when it changes: once per silence or disconnection.
    Freshness sourceState = Freshness::NotRead;

    //The mode the unit reports, auto included.
    bool unitMode(ClimateMode &mode);
    //Writes the mode to the unit and dates the switch for the lock-outs.
    bool writeUnitMode(ClimateMode mode);
    void startEmulation();
    //Auto selected: the device takes over from the mode the unit is in.
    bool startAuto();
    void recreateRange();
    void restoreEmulation();
    void saveEmulation();
    //remote_temp_source when linked, else current_temp.
    const char *referenceRole();
    void emulate();
    void regulate();
    //Holds the unit as it is while the room temperature is not fresh.
    bool referenceFresh();
    //The unit's setpoint follows the bound of the mode it runs in.
    void followSetpoint(double value);
    void suspend(ClimateMode reported);
    void publishStatusChange();

    struct Emulation
    {
        //emulate_range is set and the device can honour it.
        bool enabled = false;
        //Calaos is in auto: the device drives the unit.
        bool active = false;
        //What the device last sent to the unit, and when (loop ms).
        ClimateMode commanded = ClimateMode::Off;
        std::optional<uint64_t> commandedAt;
        //The mode before that command, which the unit may still republish.
        ClimateMode previous = ClimateMode::Off;
        //The last of heat and cool sent: a restart waits for a changeover only towards the other.
        ClimateMode lastActive = ClimateMode::Off;
        std::optional<double> setpoint;
        std::optional<uint64_t> setpointAt;

        double hysteresis = DEFAULT_HYSTERESIS;
        Lockouts lockouts;
        //Loop times of the last switches of heat and of cool, and of the start.
        std::optional<uint64_t> heatOnAt, heatOffAt, coolOnAt, coolOffAt, startedAt;
        std::unique_ptr<Timer> lockout;
        uint64_t lockoutUntil = 0;
        //A wanted heat or cool waiting for its lock-out, and when it may start.
        ClimateMode waiting = ClimateMode::Off;
        std::optional<uint64_t> startsAt;

        Freshness reference = Freshness::NotRead;
        string fault;
        string publishedStatus;
        bool evaluating = false;
        bool again = false;
        vector<IOChangeHub::Subscription> triggers;
        std::unique_ptr<Timer> freshnessCheck;
    } emu;
};

} //namespace Calaos

#endif
