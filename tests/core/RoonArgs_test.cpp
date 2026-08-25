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

/*******************************************************************************
 * T3.28 - CHARACTERIZATION of the Roon launch arguments.
 *
 * Two defects, both in Audio/RoonPlayer.cpp, and this file is written BEFORE
 * either is fixed. It is RED on master and it must stay red until the fix
 * commit lands.
 *
 * (a) THE PORT NEVER REACHES THE ioDoc AS A DEFAULT.
 *     RoonPlayer.cpp:180 calls
 *         ioDoc->paramAdd("port", ..., IODoc::TYPE_INT, 9330);
 *     and IODoc.h:46 is
 *         paramAdd(name, description, type, bool mandatory,
 *                  const string defaultval = string(), bool readonly = false)
 *     so 9330 lands in `mandatory`, converts to `true` without a single
 *     warning, and `defaultval` keeps its default "". IODoc.cpp:59 then does
 *         if (!defaultval.empty()) param.Add("default", defaultval);
 *     so the document of the "port" parameter carries NO "default" key at all
 *     and says mandatory=true. That is what calaos_installer shows the user:
 *     a required port with no suggested value, on a parameter whose own
 *     description says "empty to autodetect".
 *
 * (b) THE RESPAWN DROPS --host AND --port.
 *     RoonPlayer.cpp:39-50: the processExited handler calls
 *     startProcess(exe, "roon") with no third argument, while the first
 *     launch at :50 passes `args`. `args` is built at :46-48, i.e. AFTER the
 *     connect(), so the [=] lambda could not have captured it anyway. From
 *     the first restart on, a statically configured core is replaced by
 *     whatever RoonDiscovery finds.
 *
 * ---------------------------------------------------------------------------
 * WHY (b) AND THE PORT MEMBER ARE PINNED BY SOURCE TRIPWIRES AND NOT BY A
 * BEHAVIOURAL CASE
 * ---------------------------------------------------------------------------
 * RoonCtrl::Instance() (RoonPlayer.cpp:58-60) is a static singleton whose
 * constructor builds an ExternProcServer - which binds a unix socket - and
 * spawns calaos_roon. E4.1h measured this and the review closed the question:
 * "the blocker is not the link, it is the singleton that binds a socket and
 * launches the process" (FINDINGS.md:109-113). Nothing in `make check` can
 * construct a RoonCtrl, so no test can observe which arguments its two
 * startProcess() call sites pass.
 *
 * What CAN be observed is the shipped source, through CALAOS_TOP_SRCDIR - the
 * mechanism tests/JanssonResidues_test.cpp already uses for the same reason.
 * The tripwire below counts the startProcess( call sites of RoonPlayer.cpp:
 * two today, one after the fix, because the fix routes both the first launch
 * and the respawn through a single private launch() that uses a single
 * argument string. That single call site is a STRUCTURAL closure - the two
 * paths cannot diverge because there is only one path - and the tripwire is
 * what keeps a future edit from re-opening it.
 *
 * ⚠️ Stated plainly so nobody credits this suite with more than it has: a
 * source tripwire is a WEAKER oracle than a behavioural case. It cannot tell
 * you the surviving call site passes the RIGHT string; the buildArgs() cases
 * added by the fix commit do that, on production code. The two together are
 * the net, neither alone.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE DELIBERATELY DOES NOT TOUCH
 * ---------------------------------------------------------------------------
 *  - Utils::from_string(). Its "returns true and writes nothing on a blank
 *    string" behaviour is T3.25's perimeter, and T3.25 is in flight. This
 *    ticket must not depend on it either way: RoonArgs::portFromParams() is
 *    written so that its answer is the same before and after T3.25 lands.
 *  - The respawn having NO BACKOFF AT ALL (seven controllers out of eight,
 *    FINDINGS.md:2510-2518). That is a separate, cross-cutting ticket.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "CalaosCoreFixture.h"
#include "IOBase.h"
#include "IODoc.h"
#include "Params.h"
#include "RoonPlayer.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

#ifndef CALAOS_TOP_SRCDIR
#error "CALAOS_TOP_SRCDIR must be passed by the build (see tests/Makefile.am)"
#endif

//Read a shipped source file. Answers false when the file cannot be opened, so
//a mis-wired path FAILS the case instead of quietly asserting on "".
bool readShippedSource(const std::string &relative, std::string &out)
{
    const std::string path = std::string(CALAOS_TOP_SRCDIR) + "/" + relative;
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open())
        return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int countOccurrences(const std::string &haystack, const std::string &needle)
{
    int n = 0;
    for (std::string::size_type p = haystack.find(needle);
         p != std::string::npos;
         p = haystack.find(needle, p + needle.size()))
        n++;
    return n;
}

/*
 * The document of ONE parameter of an IO, by name.
 *
 * Same walk as core/WebIO_test.cpp's docParamNames(): IODoc::genDocJson()
 * answers {"parameters": [ {...}, ... ]} and the array order is a libstdc++
 * unordered_map detail, so the lookup is by "name" and never by index.
 * Answers a discarded Json when the parameter is not documented at all, which
 * is a different failure from "documented with the wrong value" and must read
 * differently in the log.
 */
Json docParam(IOBase *io, const std::string &name)
{
    Json doc = io->getDoc()->genDocJson();
    for (const Json &val: doc.value("parameters", Json::array()))
    {
        if (val.is_object() && val.value("name", std::string()) == name)
            return val;
    }
    return Json(Json::value_t::discarded);
}

/*
 * A Params for a RoonPlayer that STOPS SHORT OF THE SINGLETON.
 *
 * RoonPlayer's constructor returns at :190 when zone_id is empty, before
 * RoonCtrl::Instance() - so an empty zone_id is the one shape that documents
 * itself, reads its parameters and spawns nothing. Everything this suite
 * asserts (the ioDoc, and later the port member) is already settled by then.
 *
 * ⚠️ FIXTURE, NOT DECORATION: the host and the port here are DELIBERATELY not
 * the defaults. 9331 is one away from the 9330 default precisely so that
 * "the default was applied" and "the configured value was read" cannot be
 * confused; a fixture on 9330 would be green under both the defect and the
 * fix. Same reason for the host: an empty host produces no arguments at all,
 * so it can never distinguish a working emitter from a silent one.
 */
Params roonParams(const std::string &id)
{
    Params p;
    p.Add("id", id);
    p.Add("name", id);
    p.Add("type", "Roon");
    p.Add("zone_id", "");        //stop before RoonCtrl::Instance()
    return p;
}

} // namespace

class RoonArgsTest: public CoreFixture {};

/*
 * ⭐ DEFECT (a), the half the user sees: what calaos_installer is told about
 * "port".
 *
 * RED on master on both counts - mandatory is "true" and there is no
 * "default" key - because 9330 was eaten by the bool parameter.
 *
 * The "type" assertion is ACQUIS, not a defect: it is here so that a fix
 * reaching for paramAddFloat() or paramAddList() by accident does not slip
 * through, and so that the case names the whole contract rather than only its
 * broken half.
 */
TEST_F(RoonArgsTest, TheIoDocDeclaresPortOptionalWithADefault)
{
    Params p = roonParams("roon_doc");
    RoonPlayer player(p);

    const Json port = docParam(&player, "port");
    ASSERT_FALSE(port.is_discarded()) << "the \"port\" parameter is not documented at all";

    EXPECT_EQ("int", port.value("type", std::string()));
    EXPECT_EQ("false", port.value("mandatory", std::string()))
        << "port is declared MANDATORY: 9330 landed in the bool parameter of "
           "paramAdd(), see IODoc.h:46";
    EXPECT_EQ("9330", port.value("default", std::string()))
        << "port carries no default: paramAdd() only adds a \"default\" key "
           "when defaultval is non-empty, and defaultval stayed \"\"";
}

/*
 * ACQUIS - the "host" parameter next door is declared correctly, and stays
 * so. It is the control of the case above: if BOTH went red, the failure is
 * in the walk, not in the declaration of "port".
 */
TEST_F(RoonArgsTest, TheIoDocStillDeclaresHostOptional)
{
    Params p = roonParams("roon_doc_host");
    RoonPlayer player(p);

    const Json host = docParam(&player, "host");
    ASSERT_FALSE(host.is_discarded()) << "the \"host\" parameter is not documented at all";

    EXPECT_EQ("string", host.value("type", std::string()));
    EXPECT_EQ("false", host.value("mandatory", std::string()));
}

/*
 * ⭐ DEFECT (b): the respawn must launch calaos_roon exactly as the first
 * launch did.
 *
 * The oracle is the number of startProcess( call sites in the shipped
 * RoonPlayer.cpp. Two on master - one at :43 with no arguments, one at :50
 * with them. One after the fix, inside the private launch() both paths go
 * through. See the header comment for why this cannot be a behavioural case.
 *
 * This is also the case the counter-mutation of the delivery targets: putting
 * `process->startProcess(exe, "roon");` back into the handler brings the
 * count to two and reddens THIS case and this case only.
 */
TEST_F(RoonArgsTest, TripwireSource_TheRespawnLaunchesThroughTheSameCallSite)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/Audio/RoonPlayer.cpp", src))
        << "could not read the shipped RoonPlayer.cpp under " << CALAOS_TOP_SRCDIR;

    EXPECT_EQ(1, countOccurrences(src, "startProcess("))
        << "RoonPlayer.cpp must launch calaos_roon from ONE place, so the "
           "respawn cannot pass different arguments than the first launch";
}

/*
 * The belt of the braces: RoonPlayer.h must give `port` an in-class
 * initialiser.
 *
 * ⚠️ THIS TRIPWIRE IS NOT REDUNDANT WITH THE BEHAVIOURAL PORT CASES, and that
 * is worth stating because it looks like it is. Once portFromParams() answers
 * 9330 for an absent or blank parameter, an UNINITIALISED member and a member
 * initialised to 9330 produce exactly the same observable behaviour - the
 * assignment overwrites both. No behavioural test can tell them apart. The
 * initialiser is there for the day someone adds an early return above the
 * assignment, and only a source oracle can pin it.
 *
 * RED on master: RoonPlayer.h:214 is `int port;`, and the constructor's
 * initialiser list (:172-173) carries only AudioPlayer(p). On a blank
 * param["port"], Utils::from_string() answers true WITHOUT WRITING - the
 * stream sentry fails before extraction (StringUtils.h:104-111, measured in
 * T3.25) - so port keeps whatever was on the stack. Not 0. Indeterminate.
 */
TEST_F(RoonArgsTest, TripwireSource_ThePortMemberCarriesAnInClassInitialiser)
{
    std::string hdr;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/Audio/RoonPlayer.h", hdr))
        << "could not read the shipped RoonPlayer.h under " << CALAOS_TOP_SRCDIR;

    EXPECT_EQ(1, countOccurrences(hdr, "int port ="))
        << "RoonPlayer::port has no in-class initialiser, so a from_string() "
           "that writes nothing leaves it indeterminate";
}
