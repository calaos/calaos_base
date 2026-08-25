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
 * Two defects, both in Audio/RoonPlayer.cpp. The first four cases of this
 * file were written and committed BEFORE either was fixed and were RED on
 * master (eb369987); the behavioural half at the bottom was added once
 * Audio/RoonArgs.h existed to be called, and every one of its cases was then
 * proved to bite by mutating the SHIPPED code - see the delivery sheet of
 * T3.28 for the campaign and its two disjoint red sets.
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
 * The tripwire below counts the spawn call sites of RoonPlayer.cpp: two on
 * master, one after the fix, because the fix routes both the first launch and
 * the respawn through a single private launch() that uses a single argument
 * string. That single call site is a STRUCTURAL closure - the two paths
 * cannot diverge because there is only one path - and the tripwire is what
 * keeps a future edit from re-opening it.
 *
 * ⚠️ A SOURCE TRIPWIRE COUNTS TEXT, SO IT MUST COUNT THE TEXT THAT MATTERS.
 * The first version of that tripwire counted `startProcess(` - the call NAME -
 * and the review of this ticket showed what that buys: exchanging the shipped
 * `startProcess(exe, "roon", procArgs)` for `startProcess(exe, "roon",
 * std::string())` left the count at one and the whole suite GREEN. The sidecar
 * could be restarted with no arguments at all - the defect this ticket exists
 * to fix - without a single red. Both tripwires below now pin the full
 * spelling of what they guard, not the fragment that identifies it: the launch
 * site pins its argument list, and the `port` member pins its initialiser's
 * VALUE and not merely the presence of an `=`.
 *
 * ⚠️ Stated plainly so nobody credits this suite with more than it has: a
 * source tripwire is still a WEAKER oracle than a behavioural case, and
 * pinning the spelling does not change its nature. It says the call passes
 * `procArgs`; it cannot say `procArgs` HOLDS the right string - the buildArgs()
 * cases at the bottom do that, on production code, and they would not notice a
 * call site that vanished. The two together are the net, neither alone. What
 * NOTHING here can prove is that calaos_roon then does the right thing with
 * those flags: no test on real Roon hardware was run for this ticket, and the
 * delivery sheet says so.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS SUITE DELIBERATELY DOES NOT TOUCH
 * ---------------------------------------------------------------------------
 *  - Utils::from_string(). Its "returns true and writes nothing on a blank
 *    string" behaviour is T3.25's perimeter, and T3.25 is in flight. This
 *    ticket must not depend on it either way: RoonArgs::portFromParams() is
 *    written so that its answer is the same before and after T3.25 lands.
 *  - The respawn having NO BACKOFF AT ALL (seven controllers out of eight,
 *    FINDINGS.md, E4.5d, "Sept controleurs sur huit respawnent leur
 *    sous-processus sans aucun delai" - cited by title because the line
 *    number this file shipped with was already stale). That is a separate,
 *    cross-cutting ticket.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "CalaosCoreFixture.h"
#include "IOBase.h"
#include "IODoc.h"
#include "Params.h"
#include "RoonArgs.h"
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
 * Collapse every run of whitespace into a single space.
 *
 * ⚠️ THIS IS WHAT MAKES IT AFFORDABLE TO PIN A WHOLE CALL AND NOT JUST ITS
 * NAME. A tripwire that counts `startProcess(` pins the EXISTENCE of the call;
 * one that counts `startProcess(exe, "roon", procArgs);` pins WHAT IT PASSES,
 * which is the only thing this ticket is about. The price of the second form
 * is that it goes red when someone re-wraps the call across two lines or
 * re-indents the file - a false red, and false reds are how tripwires get
 * deleted. Collapsing whitespace first removes that price: the needle then
 * matches any layout of the same tokens.
 *
 * It does NOT normalise spaces *around* punctuation, so `startProcess( exe ,
 * ... )` would still miss. That is a loud red with a message naming the
 * spelling it wants, not a silent survival, and the two are not the same
 * failure: the whole point of the hardening is that a WRONG argument list can
 * no longer pass unnoticed.
 *
 * Runs inside string literals are collapsed too. Neither file read here
 * contains a literal with two adjacent spaces; should one appear, this must be
 * revisited rather than trusted.
 */
std::string collapseWhitespace(const std::string &src)
{
    std::string out;
    out.reserve(src.size());

    bool inRun = false;
    for (std::string::size_type i = 0; i < src.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
        {
            if (!inRun) out += ' ';
            inRun = true;
        }
        else
        {
            out += src[i];
            inRun = false;
        }
    }

    return out;
}

/*
 * Drop C and C++ comments, keeping string and char literals intact.
 *
 * ⚠️ THIS IS NOT COSMETIC. A tripwire that counts a call spelling over the RAW
 * bytes of a source file also counts every mention of it in PROSE, so the
 * comment that explains the tripwire would itself break the tripwire - and a
 * maintainer would "fix" it by rewording a comment, learning nothing. Counting
 * over the CODE only makes the oracle mean what its name says.
 *
 * It is a lexer, not a parser: it is deliberately blind to raw string literals
 * (R"(...)"), which neither of the two files it reads contains. Should one
 * appear, this must be revisited rather than trusted.
 */
std::string stripComments(const std::string &src)
{
    std::string out;
    out.reserve(src.size());

    enum { Code, LineComment, BlockComment, StringLit, CharLit } st = Code;

    for (std::string::size_type i = 0; i < src.size(); i++)
    {
        const char c = src[i];
        const char n = (i + 1 < src.size())? src[i + 1] : '\0';

        switch (st)
        {
        case Code:
            if (c == '/' && n == '/')      { st = LineComment; i++; }
            else if (c == '/' && n == '*') { st = BlockComment; i++; }
            else
            {
                if (c == '"')       st = StringLit;
                else if (c == '\'') st = CharLit;
                out += c;
            }
            break;
        case LineComment:
            if (c == '\n') { st = Code; out += c; }
            break;
        case BlockComment:
            if (c == '*' && n == '/') { st = Code; i++; }
            else if (c == '\n')       out += c;   //keep line numbering readable
            break;
        case StringLit:
        case CharLit:
            out += c;
            if (c == '\\' && i + 1 < src.size()) { out += n; i++; }
            else if ((st == StringLit && c == '"') || (st == CharLit && c == '\''))
                st = Code;
            break;
        }
    }

    return out;
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
 * launch did, WITH THE ARGUMENTS.
 *
 * TWO oracles on the shipped RoonPlayer.cpp, and they answer two different
 * questions. Both are needed; neither implies the other.
 *
 *  1. HOW MANY launch sites there are. Two on master - one at :43 with no
 *     arguments, one at :50 with them. One after the fix, inside the private
 *     launch() both paths go through. This is the STRUCTURAL closure: the two
 *     paths cannot diverge because there is only one path left.
 *
 *  2. WHAT THAT ONE SITE PASSES, spelled out in full. Because oracle 1 counts
 *     the call NAME, it is blind to the arguments: the review of this ticket
 *     exchanged `startProcess(exe, "roon", procArgs)` for
 *     `startProcess(exe, "roon", std::string())` and the WHOLE SUITE STAYED
 *     GREEN - a sidecar restarted with no arguments at all, which is the very
 *     defect this ticket fixes, reintroduced without a single red. The count
 *     was still one. So the argument list is pinned here, literally.
 *
 * ⚠️ Oracle 2 does NOT make this a behavioural case and must not be read as
 * one. It pins the TEXT of a call, not its effect: it cannot tell you that
 * `procArgs` holds the right string - the buildArgs() cases at the bottom do
 * that, on production code - and it would be satisfied by a `procArgs` that
 * was never assigned. What it does close is the one hole a name-only count
 * leaves wide open, at the cost of one line.
 *
 * Both mutations of the delivery campaign land HERE and nowhere else: putting
 * `process->startProcess(exe, "roon");` back into the handler reddens oracle 1
 * (and 2), and blanking the third argument reddens oracle 2.
 */
TEST_F(RoonArgsTest, TripwireSource_TheRespawnLaunchesThroughTheSameCallSite)
{
    std::string src;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/Audio/RoonPlayer.cpp", src))
        << "could not read the shipped RoonPlayer.cpp under " << CALAOS_TOP_SRCDIR;

    const std::string code = collapseWhitespace(stripComments(src));

    EXPECT_EQ(1, countOccurrences(code, "startProcess("))
        << "RoonPlayer.cpp must launch calaos_roon from ONE place, so the "
           "respawn cannot pass different arguments than the first launch";

    EXPECT_EQ(1, countOccurrences(code, "process->startProcess(exe, \"roon\", procArgs);"))
        << "the single launch site must hand calaos_roon the argument string "
           "built in the constructor: exactly "
           "`process->startProcess(exe, \"roon\", procArgs);`. A launch with "
           "no arguments, or with a different string, is the defect T3.28 "
           "fixes - and counting the call NAME alone cannot see it";
}

/*
 * The belt of the braces: RoonPlayer.h must initialise `port` in-class TO THE
 * DEFAULT PORT.
 *
 * ⚠️ THIS TRIPWIRE IS NOT REDUNDANT WITH THE BEHAVIOURAL PORT CASES, and that
 * is worth stating because it looks like it is. Once portFromParams() answers
 * 9330 for an absent or blank parameter, an UNINITIALISED member and a member
 * initialised to 9330 produce exactly the same observable behaviour - the
 * assignment overwrites both. No behavioural test can tell them apart. The
 * initialiser is there for the day someone adds an early return above the
 * assignment, and only a source oracle can pin it.
 *
 * ⚠️ AND IT PINS THE VALUE, NOT MERELY THE PRESENCE OF AN `=`. The first
 * version of this case counted `int port =`, while its own failure message
 * said "has no in-class initialiser". The review exchanged
 * `int port = RoonArgs::DefaultPort;` for `int port = 0;` and the suite stayed
 * green: the guard held the presence and the message promised the meaning. A
 * message that overstates its guard is worse than a guard that admits its
 * limits, because the next reader trusts the message. The value is what the
 * scenario above is about - on that hypothetical early return, `0` would hand
 * the sidecar `--port 0`, the exact shape of the bug being fixed - so the
 * value is what gets pinned, and the message now says exactly that.
 *
 * RED on master: RoonPlayer.h:214 is `int port;`, and the constructor's
 * initialiser list (:172-173) carries only AudioPlayer(p). On a blank
 * param["port"], Utils::from_string() answers true WITHOUT WRITING - the
 * stream sentry fails before extraction (StringUtils.h:104-111, measured in
 * T3.25) - so port keeps whatever was on the stack. Not 0. Indeterminate.
 */
TEST_F(RoonArgsTest, TripwireSource_ThePortMemberIsInitialisedToTheDefaultPort)
{
    std::string hdr;
    ASSERT_TRUE(readShippedSource("src/bin/calaos_server/Audio/RoonPlayer.h", hdr))
        << "could not read the shipped RoonPlayer.h under " << CALAOS_TOP_SRCDIR;

    EXPECT_EQ(1, countOccurrences(collapseWhitespace(stripComments(hdr)),
                                  "int port = RoonArgs::DefaultPort;"))
        << "RoonPlayer::port must be initialised in-class to "
           "RoonArgs::DefaultPort. Without an initialiser a from_string() that "
           "writes nothing leaves it indeterminate; with the WRONG initialiser "
           "an early return above the assignment would hand the sidecar a port "
           "nobody configured - `--port 0` for `int port = 0;`";
}


/*******************************************************************************
 * THE BEHAVIOURAL HALF: RoonArgs, the production assembly.
 *
 * Everything below calls Audio/RoonArgs.h - the same inline functions
 * RoonCtrl and RoonPlayer call, not a copy - or builds a real RoonPlayer and
 * reads back what it resolved. A mutation of either turns cases here red.
 ******************************************************************************/

/*
 * ⭐ ACQUIS, and the most important case of the file: an empty host must
 * produce NOTHING.
 *
 * This is the mode the parameter description advertises ("empty to autodetect
 * on network"), it is the default, and it WORKED before this ticket - which
 * is exactly why "Roon is unusable" was too strong a claim. With no argument
 * at all the sidecar runs RoonDiscovery (ExternProcRoon_main.py:75-83).
 * Passing --port alone would be worse than useless: get_roon_host() ignores
 * the port unless a host was given.
 *
 * ⚠️ The port here is 9331, NOT the 9330 default, on purpose: a fixture
 * sitting on the default cannot tell "the default was applied" from "the
 * configured value was read".
 */
TEST_F(RoonArgsTest, EmptyHostProducesNoArgumentsAtAll)
{
    EXPECT_EQ("", RoonArgs::buildArgs("", 9331));
    EXPECT_EQ("", RoonArgs::buildArgs("", RoonArgs::DefaultPort));
}

/*
 * ⭐ ACQUIS: a static host carries BOTH flags, in the shipped spelling.
 *
 * Byte for byte, leading space included - ExternProcServer::startProcess()
 * splits this string on whitespace. The host and the port are chosen so that
 * neither could be mistaken for the other if the two were ever permuted.
 */
TEST_F(RoonArgsTest, AStaticHostCarriesBothFlags)
{
    EXPECT_EQ(" --host 192.168.7.42 --port 9331",
              RoonArgs::buildArgs("192.168.7.42", 9331));
    EXPECT_EQ(" --host roon.lan --port 9330",
              RoonArgs::buildArgs("roon.lan", RoonArgs::DefaultPort));
}

/*
 * ⭐ DEFECT (a), the half that reaches the sidecar: an absent "port" resolves
 * to 9330 and not to a stack value.
 *
 * The RoonPlayer is REAL and so is the Params: this runs
 * RoonPlayer.cpp's `port = RoonArgs::portFromParams(param)` and reads the
 * member back. It is the one case that ties the production CALL SITE to the
 * production FUNCTION - portFromParams() could be perfect and the call site
 * still dropped, and only this case would notice.
 *
 * ⚠️ On master the answer was neither 9330 nor 0: Utils::from_string("")
 * answers true WITHOUT WRITING, so the member kept whatever the stack held.
 * A test asserting "0" would have been green or red depending on the weather.
 */
TEST_F(RoonArgsTest, ThePortDefaultsTo9330WhenTheParamIsAbsent)
{
    Params p = roonParams("roon_port_absent");
    ASSERT_FALSE(p.Exists("port"));

    RoonPlayer player(p);
    EXPECT_EQ(9330, player.portGet());
}

/*
 * ⭐ DEFECT (a), the shape a real configuration actually produces: the key is
 * THERE and its value is "".
 *
 * calaos_installer writes the attribute whether or not the user filled it, so
 * Exists() is true and the value is empty. This is the path that mattered in
 * the field, and it is a different code path from the absent key above -
 * Params::operator[] answers "" for both, but only this one proves it.
 */
TEST_F(RoonArgsTest, ThePortDefaultsTo9330WhenTheParamIsPresentButEmpty)
{
    Params p = roonParams("roon_port_empty");
    p.Add("port", "");
    ASSERT_TRUE(p.Exists("port"));

    RoonPlayer player(p);
    EXPECT_EQ(9330, player.portGet());
}

/*
 * ⭐ THE HOLE THE MUTATION CAMPAIGN FOUND, and the case that plugs it.
 *
 * Without this case, exchanging RoonPlayer.cpp's
 *     port = RoonArgs::portFromParams(param);
 * back for the shipped-before-T3.28
 *     Utils::from_string(param["port"], port);
 * SURVIVED the whole suite (mutant M5, measured: 0 red). The reason is
 * instructive rather than embarrassing: with the in-class initialiser in
 * place, from_string() and portFromParams() answer the SAME thing for a blank
 * value (from_string writes nothing, so 9330 survives) and for a well-formed
 * one. They part company only where from_string() writes a value nobody
 * asked for - 0 for "abc", 12 for "12abc" - or where it succeeds on a number
 * that is not a port - 70000, or an overflow clamped to INT_MAX.
 *
 * So the call site is pinned HERE, on a real RoonPlayer, and nowhere else.
 * A behavioural case on portFromParams() alone cannot do it: the function can
 * be perfect and the call site still gone.
 */
TEST_F(RoonArgsTest, ThePortDefaultsTo9330WhenTheParamIsUnreadableOrOutOfRange)
{
    {
        Params p = roonParams("roon_port_abc");
        p.Add("port", "abc");
        RoonPlayer player(p);
        //from_string() writes 0 here and answers false: 0 is not a port, and
        //"--port 0" is the very shape of the bug this ticket fixes.
        EXPECT_EQ(9330, player.portGet()) << "port=\"abc\"";
    }
    {
        Params p = roonParams("roon_port_12abc");
        p.Add("port", "12abc");
        RoonPlayer player(p);
        //from_string() writes 12 - a partial parse silently accepted.
        EXPECT_EQ(9330, player.portGet()) << "port=\"12abc\"";
    }
    {
        Params p = roonParams("roon_port_70000");
        p.Add("port", "70000");
        RoonPlayer player(p);
        //from_string() succeeds: nothing but the range test catches this one.
        EXPECT_EQ(9330, player.portGet()) << "port=\"70000\"";
    }
}

/*
 * A configured port is read, and it is NOT the default.
 *
 * The control of the two cases above: if this one went to 9330 as well,
 * portFromParams() would be answering the default unconditionally and the
 * suite would be green for the wrong reason.
 */
TEST_F(RoonArgsTest, AConfiguredPortIsReadAndIsNotTheDefault)
{
    Params p = roonParams("roon_port_set");
    p.Add("port", "9331");
    p.Add("host", "192.168.7.42");

    RoonPlayer player(p);
    EXPECT_EQ(9331, player.portGet());
    EXPECT_EQ("192.168.7.42", player.hostGet());
}

/*
 * END TO END, on the one path that broke in the field: a statically
 * configured player must hand the sidecar its OWN host and port.
 *
 * This is the case a reviewer should read first. It joins the two halves -
 * what RoonPlayer resolved out of the configuration, and what buildArgs()
 * makes of it - which is exactly the join RoonCtrl performs and no test can
 * reach directly.
 */
TEST_F(RoonArgsTest, AStaticallyConfiguredPlayerProducesTheArgumentsOfItsOwnCore)
{
    Params p = roonParams("roon_static");
    p.Add("host", "192.168.7.42");
    p.Add("port", "9331");

    RoonPlayer player(p);

    EXPECT_EQ(" --host 192.168.7.42 --port 9331",
              RoonArgs::buildArgs(player.hostGet(), player.portGet()));
}

/*
 * ⭐ THE T3.25 BOUNDARY, and the reason this suite does not depend on it.
 *
 * Utils::from_string() has three MEASURED regimes (all reproduced with a
 * standalone program, none deduced):
 *   ""  / "   "            -> answers TRUE, writes NOTHING (sentry failure);
 *   "abc" / "12abc"        -> answers FALSE, writes 0 resp. 12 (C++11);
 *   "99999999999999999999" -> answers TRUE, writes INT_MAX.
 * T3.25 is in flight and changes TWO of those three, not one - the claim that
 * it only touches the blank string was wrong and is corrected here. It returns
 * `!fail() && eof()` instead of `eof()` alone, and publishes a value-initialised
 * temporary on every path, so: the blank string answers FALSE and writes 0
 * instead of answering TRUE and writing nothing, AND the overflow answers
 * FALSE instead of TRUE while still writing INT_MAX. The middle regime is the
 * only one that comes through untouched.
 * Neither change is visible from here: portFromParams() answers 9330 in BOTH
 * worlds because it seeds its destination with DefaultPort and range-filters
 * the result, so a blank string falls back whether from_string refused it or
 * left the seed alone, and an overflow falls back whether it was refused or
 * clamped to INT_MAX. The SYMPTOM of the unfixed defect changes, the cause
 * does not.
 *
 * ⚠️ Deliberately NOT a tripwire on from_string() itself: that would go red
 * the day T3.25 merges, which is a landmine and not a net.
 */
TEST_F(RoonArgsTest, EveryUnusablePortSpellingFallsBackToTheDefault)
{
    const char *const unusable[] = {
        "",                        //sentry failure, writes nothing
        "   ",                     //same, all blank
        "abc",                     //parse failure, from_string writes 0
        "12abc",                   //partial parse, from_string writes 12
        "0",                       //parses, but 0 is not a connectable port
        "-5",                      //parses, negative
        "70000",                   //parses, above 65535
        "99999999999999999999",    //answers TRUE and writes INT_MAX
    };

    for (const char *spelling: unusable)
    {
        Params p;
        p.Add("port", spelling);
        EXPECT_EQ(RoonArgs::DefaultPort, RoonArgs::portFromParams(p))
            << "port spelled \"" << spelling << "\" should fall back to the default";
    }
}

/*
 * The range that IS accepted, at both ends and in the middle. Guards against
 * a fix that clamps too hard - 1 and 65535 are legal ports and must survive.
 */
TEST_F(RoonArgsTest, AUsablePortIsPassedThroughUnchanged)
{
    const struct { const char *spelling; int expected; } usable[] = {
        { "1", 1 },
        { "9331", 9331 },
        { "9330", 9330 },
        { "65535", 65535 },
    };

    for (const auto &c: usable)
    {
        Params p;
        p.Add("port", c.spelling);
        EXPECT_EQ(c.expected, RoonArgs::portFromParams(p)) << c.spelling;
    }
}
