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
#include "ProxyClimate.h"

#include <cmath>

using namespace Calaos;

REGISTER_DEVICE(proxy, ProxyClimate)

namespace
{

const char *const HEAT_COOL = "heat_cool";
const char *const AUTO = "auto";
const char *const MODE = "mode";
const char *const REMOTE = "remote_temp";
const char *const SOURCE = "remote_temp_source";
//Distinct unknown values logged: a broker can send an unbounded number.
const size_t MAX_LOGGED_UNKNOWN = 16;

} //namespace

ProxyClimate::ProxyClimate(Params &p):
    ClimateBase(p)
{
}

ProxyClimate::~ProxyClimate()
{
    stopListening();
}

double ProxyClimate::secondsParam(const string &name, double fallback) const
{
    double v = 0;
    if (Utils::from_string(getDeviceParam(name), v) && std::isfinite(v) && v > 0)
        return v;
    return fallback;
}

string ProxyClimate::linkIoId(const string &role) const
{
    string id;
    for (const Link &l: getLinks())
    {
        if (l.name == role)
        {
            id = l.io;
            break;
        }
    }
    return id;
}

bool ProxyClimate::isWritable(const string &role)
{
    IOBase *io = writeTarget(role);
    return io && io->isOutput();
}

void ProxyClimate::resolveLinks()
{
    ClimateBase::resolveLinks();

    const string id = get_param("id");
    if (linkIo(MODE) && !isWritable(MODE))
        cErrorDom("device") << "Device(" << id << "): link 'mode' has no command_io and its io is "
                            << "not an output, modes cannot be selected";
    if (linkIo(REMOTE) && !isWritable(REMOTE))
        cErrorDom("device") << "Device(" << id << "): link 'remote_temp' has no command_io and "
                            << "its io is not an output, the temperature cannot be relayed";
    if (linkIo(REMOTE) && !linkIo(SOURCE))
        cErrorDom("device") << "Device(" << id << "): link 'remote_temp' without a "
                            << "'remote_temp_source', nothing is relayed";
    if (linkIo(SOURCE) && !linkIo(REMOTE))
        cErrorDom("device") << "Device(" << id << "): link 'remote_temp_source' without a "
                            << "'remote_temp', nothing is relayed";

    if (hasDeviceParam("auto_payload") && getDeviceParam("auto_payload") != AUTO &&
        getDeviceParam("auto_payload") != HEAT_COOL)
        cErrorDom("device") << "Device(" << id << "): auto_payload must be 'auto' or 'heat_cool', "
                            << "ignored";
    for (const char *name: { "remote_temp_keepalive", "readback_hold" })
    {
        if (hasDeviceParam(name) && secondsParam(name, 0) == 0)
            cErrorDom("device") << "Device(" << id << "): " << name
                                << " is not a positive number of seconds, the default is used";
    }

    readbackHold = secondsParam("readback_hold", DEFAULT_READBACK_HOLD);
    readbackSubscriptions.clear();
    for (const char *name: { "target_temp_low", "target_temp_high" })
    {
        const string role = name;
        if (!linkIo(role))
            continue;
        readbackSubscriptions.push_back(IOChangeHub::Instance().subscribe(
            linkIoId(role), [this, role](IOBase &)
            {
            commanded.erase(role);
            }));
    }

    startRemoteTemp();
}

vector<ClimateMode> ProxyClimate::supportedModes()
{
    if (!isWritable(MODE))
        return {};
    return ClimateBase::supportedModes();
}

bool ProxyClimate::currentMode(ClimateMode &mode)
{
    IOBase *io = linkIo(MODE);
    if (!io)
        return false;

    const string raw = io->get_value_string();
    if (raw == HEAT_COOL || raw == AUTO)
    {
        autoReported = raw;
        mode = ClimateMode::Auto;
        return true;
    }
    if (parseMode(raw, mode))
        return true;

    //Nothing reported yet is not an unknown mode.
    if (!raw.empty())
        logUnknownMode(raw);
    return false;
}

void ProxyClimate::logUnknownMode(const string &raw)
{
    if (unknownModes.size() >= MAX_LOGGED_UNKNOWN || !unknownModes.insert(raw).second)
        return;
    cWarningDom("device") << "Device(" << get_param("id") << "): the appliance reports the mode "
                          << fieldForLog(raw) << ", which is not a climate mode";
}

bool ProxyClimate::writeMode(ClimateMode mode)
{
    //Only reached through supportedModes(), which needs a writable target.
    IOBase *io = writeTarget(MODE);

    if (mode != ClimateMode::Auto)
        return io->set_value(modeName(mode));

    string payload = getDeviceParam("auto_payload");
    if (payload != AUTO && payload != HEAT_COOL)
        payload = autoReported.empty() ? HEAT_COOL : autoReported;
    return io->set_value(payload);
}

bool ProxyClimate::rangeBound(const string &role, double &value)
{
    const auto it = commanded.find(role);
    if (it != commanded.end())
    {
        const std::chrono::duration<double> age = Clock::now() - it->second.at;
        if (age.count() < readbackHold)
        {
            value = it->second.value;
            return true;
        }
        commanded.erase(it);
    }
    return ClimateBase::rangeBound(role, value);
}

bool ProxyClimate::writeRangeBound(const string &role, double value)
{
    if (!ClimateBase::writeRangeBound(role, value))
        return false;
    commanded[role] = { value, Clock::now() };
    return true;
}

void ProxyClimate::startRemoteTemp()
{
    keepalive.reset();
    sourceSubscription.reset();
    if (!linkIo(REMOTE) || !linkIo(SOURCE))
        return;

    sourceSubscription = IOChangeHub::Instance().subscribe(linkIoId(SOURCE),
                                                           [this](IOBase &)
                                                           {
        relayRemoteTemp();
    });
    keepalive = std::make_unique<Timer>(secondsParam("remote_temp_keepalive", DEFAULT_KEEPALIVE),
                                        [this]()
                                        {
        relayRemoteTemp();
    });
}

/* A source that has not delivered yet would send 0 °C and the unit would
 * regulate on it; one that reports itself disconnected is stale, and the unit
 * is better off falling back to its own sensor once the updates stop. Silence
 * from a source that is merely steady is not staleness: IOs carry no
 * timestamp, so it keeps being relayed. */
void ProxyClimate::relayRemoteTemp()
{
    IOBase *source = linkIo(SOURCE);
    IOBase *target = writeTarget(REMOTE);
    if (!source || !target || !source->hasValue())
        return;
    if (source->getStatusInfo()["connected"] == "false")
        return;

    target->set_value(source->get_value_double());
}
