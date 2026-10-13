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
 * spelling the appliance last reported, else heat_cool. A mode link that
 * cannot be written (no command_io, io not an output) offers no mode. */
class ProxyClimate: public ClimateBase
{
public:
    ProxyClimate(Params &p);
    ~ProxyClimate();

    void resolveLinks() override;
    vector<ClimateMode> supportedModes() override;

    //Used without the "remote_temp_keepalive" / "readback_hold" params (seconds).
    static constexpr double DEFAULT_KEEPALIVE = 120;
    static constexpr double DEFAULT_READBACK_HOLD = 10;

protected:
    bool currentMode(ClimateMode &mode) override;
    bool writeMode(ClimateMode mode) override;
    bool rangeBound(const string &role, double &value) override;
    bool writeRangeBound(const string &role, double value) override;

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
    bool isWritable(const string &role);
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
};

} //namespace Calaos

#endif
