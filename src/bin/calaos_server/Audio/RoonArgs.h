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
#include <vector>

#include "LogSetup.h"
#include "Params.h"
#include "Utils.h"

/*
 * T3.28 - the command line calaos_roon is launched with, in one place.
 *
 * These two functions live here, and not inside RoonCtrl, so that the
 * assembly of the command line can be called on its own, without a controller
 * and without a subprocess. tests/core/RoonArgs_test.cpp calls exactly these -
 * the same code the server ships - so a mutation of the shipped assembly turns
 * that suite red. Same shape as Audio/SqueezeboxWire.h, IO/Mqtt/MqttWire.h,
 * IO/Reolink/ReolinkWire.h, IO/Wago/WagoWire.h and IO/OLA/OLAWire.h.
 *
 * ⛔ T3.28b - THE REASON THIS COMMENT USED TO GIVE WAS FALSE. It read: "nothing
 * in `make check` can construct a RoonCtrl; E4.1h measured it and the review
 * closed the question". Every clause about the constructor is true - it does
 * build an ExternProcServer, it does bind a unix socket, it does spawn the
 * python sidecar - and the conclusion is not. Instance() is public and static,
 * and a spawn is an OBSERVATION POINT: point CALAOS_BIN_PREFIX at a directory
 * of your own and the launch reads back from what the kernel handed the child.
 * tests/core/RoonArgs_test.cpp and tests/core/RoonSpawnViaPlayer_test.cpp both
 * do it on every `make check`. See docs/refactoring/FINDINGS.md, F-LINK-1.
 * The extraction stays - it is good for its own sake - but it is no longer
 * justified by an impossibility that does not exist.
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
 * T3.25 is in flight and changes TWO of those three regimes, not one. It
 * returns `!fail() && eof()` instead of `eof()` alone and publishes a
 * value-initialised temporary on every path, so the blank string answers FALSE
 * and writes 0 (instead of TRUE and nothing) AND the overflow answers FALSE
 * while still writing INT_MAX (instead of TRUE). Only the middle regime comes
 * through unchanged. This function is written to answer the same thing before
 * and after: `parsed` is SEEDED with DefaultPort, so the "wrote nothing" case
 * and the "wrote the default" case coincide, and every value the other regimes
 * can leave behind - 0, 12, INT_MAX - is filtered by the range test below,
 * whichever way the return value went.
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
 * The arguments calaos_roon is started with, one per argv.
 *
 * ⭐ AN EMPTY HOST MUST PRODUCE NO ARGUMENT AT ALL, and that is the acquis this
 * must not break. With no `--host`, the sidecar runs RoonDiscovery
 * (ExternProcRoon_main.py:75-83) and finds the core on the network; that is
 * the default mode, it is what the parameter description promises. Passing
 * `--port` alone would not help either: get_roon_host() ignores the port
 * entirely unless a host was given.
 *
 * ⚠️ THE SPACE GUARD SURVIVES, AND ITS REASON CHANGED. It was a
 * transport workaround: startProcess() re-split the command line, so a space
 * made an extra argv and argparse exited 2 in a 100 ms respawn loop. Nothing
 * is re-split any more - a host with a space would now travel whole. It is
 * kept as a POLICY: `mon core` is one typo away from `moncore`, the installer
 * accepts it without a word, and falling back to discovery serves the user
 * better than a name that can never resolve. A tab or a newline is still let
 * through, for the same reason as before: nothing measurable breaks on them.
 */
inline std::vector<std::string> buildArgs(const std::string &host, int port)
{
    if (host.empty())
        return std::vector<std::string>();

    if (host.find(' ') != std::string::npos)
    {
        cWarningDom("roon") << "Ignoring the configured Roon host \"" << host
                            << "\": the \"host\" parameter cannot contain a "
                               "space. Falling back to autodetection on the "
                               "network.";
        return std::vector<std::string>();
    }

    return { "--host", host, "--port", Utils::to_string(port) };
}

} //namespace RoonArgs

#endif // S_ROON_ARGS_H
