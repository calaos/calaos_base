/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef S_ROON_ARGS_H
#define S_ROON_ARGS_H

#include <string>

#include "Params.h"
#include "Utils.h"

/*
 * T3.28 - the command line calaos_roon is launched with, in one place.
 *
 * These two functions live here, and not inside RoonCtrl, because
 * RoonCtrl::Instance() is a static singleton whose constructor builds an
 * ExternProcServer - binding a unix socket - and spawns the python sidecar.
 * Nothing in `make check` can construct one; E4.1h measured it and the review
 * closed the question (FINDINGS.md:109-113). Free functions CAN be called from
 * a test, and tests/core/RoonArgs_test.cpp calls exactly these - the same code
 * the server ships - so a mutation of the shipped assembly turns that suite
 * red. Same shape as Audio/SqueezeboxWire.h, IO/Mqtt/MqttWire.h,
 * IO/Reolink/ReolinkWire.h, IO/Wago/WagoWire.h and IO/OLA/OLAWire.h.
 *
 * ⚠️ NO NAMED WRAPPER TYPE HERE, DELIBERATELY, and the reason is measured
 * rather than aesthetic. The series introduced LmsHost / LightState /
 * RedChannel because permuting two positional arguments of the SAME type at a
 * call site compiles and ships, and no test can see a call site. buildArgs()
 * takes a std::string and an int: permuting them does not compile, so a
 * wrapper would buy nothing that the type system does not already give.
 */
namespace RoonArgs
{

/*
 * The port a Roon core listens on when nothing says otherwise.
 *
 * ⚠️ THIS LITERAL IS DUPLICATED IN THE SIDECAR, on purpose and unavoidably:
 * ExternProcRoon_main.py:14 (`self.roon_port = 9330`) and :43
 * (`parser.add_argument('--port', ..., default=9330)`). The python default
 * only fires when the flag is ABSENT, which is exactly what made the old
 * defect invisible in autodetect mode and fatal with a static host - the C++
 * side always passed the flag as soon as `host` was set, so the python
 * default never got a chance to apply.
 */
static const int DefaultPort = 9330;

/*
 * The port to hand to the sidecar, read off the IO configuration.
 *
 * ⚠️ WHAT THIS ANSWERS, AND WHY IT DOES NOT DEPEND ON T3.25.
 * Utils::from_string() (src/lib/StringUtils.h:104-111) has three regimes, and
 * they were MEASURED, not deduced:
 *   - ""  and any all-blank string : answers TRUE and WRITES NOTHING. The
 *     stream sentry fails before extraction ever runs, so `dest` keeps
 *     whatever it held. Not 0 - unchanged.
 *   - "abc", "12abc"               : answers FALSE and writes 0 resp. 12
 *     (C++11 num_get stores a value on failure, unlike C++03).
 *   - "99999999999999999999"       : answers TRUE and writes INT_MAX.
 * T3.25 is in flight and changes the FIRST regime so that a defined value is
 * written on failure. This function is written to answer the same thing
 * before and after that change: `parsed` is SEEDED with DefaultPort, so the
 * "wrote nothing" case and the "wrote the default" case coincide, and every
 * other regime is filtered by the range test below.
 *
 * ⚠️ DELIBERATE DEVIATION FROM THE TICKET, stated so a reviewer does not have
 * to guess: T3.28.md §3 suggested a declared range of 0..65535 for the ioDoc.
 * The floor here and in RoonPlayer's paramAddInt() is 1, not 0. Port 0 is not
 * a connectable TCP port - it means "any free port" to bind(), and nothing at
 * all to connect() - and letting it through would hand `--port 0` to the
 * sidecar, which is the very shape of the bug being fixed. Declaring 0..65535
 * while refusing 0 here would also make the installer's advertised range and
 * the accepted range disagree.
 */
inline int portFromParams(const Params &param)
{
    int parsed = DefaultPort;

    //An unreadable value is refused, NOT coerced: from_string() writes 0 for
    //"abc" and 12 for "12abc", and neither is a port the user asked for.
    if (!Utils::from_string(param["port"], parsed))
        return DefaultPort;

    //Catches the overflow regime (INT_MAX with a true answer), a negative
    //value, and 0.
    if (parsed < 1 || parsed > 65535)
        return DefaultPort;

    return parsed;
}

/*
 * The argument string calaos_roon is started with.
 *
 * ⭐ AN EMPTY HOST MUST PRODUCE AN EMPTY STRING, and that is the acquis this
 * fix must not break. With no `--host`, the sidecar runs RoonDiscovery
 * (ExternProcRoon_main.py:75-83) and finds the core on the network; that is
 * the default mode, it is what the parameter description promises, and it was
 * working before this ticket. Passing `--port` alone would not help either:
 * get_roon_host() ignores the port entirely unless a host was given.
 *
 * The leading space and the exact spelling of the two flags are the shipped
 * form, byte for byte - ExternProcServer::startProcess() splits this string on
 * whitespace before handing it to uvw.
 */
inline std::string buildArgs(const std::string &host, int port)
{
    if (host.empty())
        return std::string();

    return " --host " + host + " --port " + Utils::to_string(port);
}

} //namespace RoonArgs

#endif // S_ROON_ARGS_H
