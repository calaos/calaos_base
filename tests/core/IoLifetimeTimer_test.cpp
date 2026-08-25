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
 **  along with Calaos; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/

/******************************************************************************
 * T3.40 - a fire-and-forget one-shot must not outlive the IO that armed it.
 *
 * Timer::singleShot()/Idler::singleIdler() (src/lib/Timer.cpp) copy the slot
 * into an ANONYMOUS uvw handle. Nothing holds that handle afterwards: no
 * destructor and no member can cancel it. A slot that holds `this` therefore
 * keeps running after the object is gone, and sigc::trackable does not help -
 * it only ever disconnects a sigc::mem_fun, never a lambda capture
 * (FINDINGS.md / F-SIGC-1).
 *
 * class IOBase does not derive from anything, and every IO in the tree can be
 * destroyed while the loop is still running: ListeRoom::deleteIO() (reachable
 * from the JSON API), Room::RemoveIO(), ~Room()/~ListeRoom() when the
 * configuration is reloaded or the server stops. T3.34 measured the
 * consequence on the two shutter IOs - SIGSEGV, exit 139, and not a single
 * FAILED line - and guarded its own four sites. This suite covers the same
 * defect on the IOs that were left out.
 *
 * ---------------------------------------------------------------------------
 * THE ORACLE: a caller-owned buffer, poisoned after destruction
 * ---------------------------------------------------------------------------
 * A use-after-free does not have to crash, so "the binary survived" proves
 * nothing and the test must not depend on the allocator either. Each probe is
 * therefore built with placement new INTO A BUFFER THE TEST OWNS, destroyed
 * with an explicit destructor call, and the buffer is then filled with a
 * poison byte. Nothing else can ever write there: the storage is a member of
 * the test object, no allocator can hand it out again, and the loop is only
 * pumped afterwards.
 *
 *   - if the orphan callback writes through its dangling `this`, the poison
 *     is broken at that offset and the case fails;
 *   - if it dereferences the poison, the binary dies (exit 139) and there is
 *     NO FAILED line - which is why this suite is judged on the exit code
 *     first and the log second.
 *
 * 0xA5 is deliberately not a value any of the callbacks writes: they all
 * write 0/false, so "never written" and "written the broken value" cannot be
 * confused (the 7th false-green variant of the tree's method notes: when 0 is
 * a value of the domain, the sentinel must be outside it).
 *
 * PoisonDetectorSeesAWriteIntoTheFreedStorage is the self-test of that oracle:
 * a detector that cannot see a write would make every case in this file
 * vacuously green.
 *
 * ---------------------------------------------------------------------------
 * THE WINDOW IS REAL, AND IT IS A SHORT POSITIVE DELAY
 * ---------------------------------------------------------------------------
 * Measured in this image by T3.34 (libuv 1.44.2): singleShot(0) fires on the
 * NEXT loop turn, when the object is still alive, and singleShot(-0.001)
 * passes an overflowing deadline that libuv clamps to "never", so the one-shot
 * never fires at all. Neither opens a window. Only a SHORT POSITIVE delay
 * does, and that is what the IOs covered here use: 250 ms
 * (Scenario/InputSwitchLongPress/InputSwitchTriple) and 1.5 s (KNXIo).
 * *TheResetStillRunsWhileTheIoIsAlive pins both ends of that window: the value
 * is untouched well inside the delay and reset well after it.
 *
 * The one 0-delay site covered here (RoonPlayer::get_playlist_size) is not a
 * counter-example: its window is not a delay, it is the fact that nothing
 * pumps the loop between arming the one-shot and destroying the IO - which is
 * exactly what a synchronous deleteIO() coming from the JSON API does.
 *
 * ---------------------------------------------------------------------------
 * SUITE ORDER MATTERS
 * ---------------------------------------------------------------------------
 * The suites that KILL the binary rather than fail are declared LAST, after
 * the ones that merely go red: before the guard, IoLifetimeExternProcTest and
 * IoLifetimeKnxTest dereference the dangling `this` straight away, gtest emits
 * no FAILED line, and nothing declared after them is ever reached. Order:
 * source guard (touches neither loop nor IO), poisoned storage, Roon (a
 * counter, never a crash), then the two killers.
 *
 * Own main() with _exit(), same reason as core/KnxIo_test: KNXCtrl/RoonCtrl
 * live in function-local statics whose destruction tears down an
 * ExternProcServer whose own destructor sends SIGTERM to pid 0 after a failed
 * spawn - i.e. to our whole process group, killing the automake harness.
 ******************************************************************************/

#include <gtest/gtest.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "CalaosCoreFixture.h"
#include "RoonSpawnHarness.h"
#include "libuvw.h"

#include "InputSwitch.h"
#include "InputSwitchLongPress.h"
#include "InputSwitchTriple.h"
#include "Scenario.h"
#include "ExternProc.h"
#include "RoonPlayer.h"
#include "KNX/KNXIo.h"

using namespace Calaos;
using namespace CalaosTest;

namespace
{

/* ---- loop helpers (same discipline as core/Timer_test) ---------------- */

/* Pump the default loop for a fixed wall clock duration. Every wait in this
 * file is bounded, so a regression fails instead of hanging make check. */
void pumpLoopFor(int ms)
{
    auto loop = uvw::Loop::getDefault();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(ms);

    while (std::chrono::steady_clock::now() < deadline)
        loop->run<uvw::Loop::Mode::NOWAIT>();
}

/* ---- the poisoned storage oracle -------------------------------------- */

const unsigned char kPoison = 0xA5;

/* Storage for one probe, owned by the test. construct() places the object in
 * it, destroyAndPoison() runs the destructor and overwrites every byte with
 * kPoison. breaches() counts the bytes that are no longer poison, i.e. the
 * bytes a callback wrote through a dangling `this`. */
template <typename T>
class OwnedIoStorage
{
public:
    template <typename... A>
    T *construct(A &&... args)
    {
        obj = new (buf) T(std::forward<A>(args)...);
        return obj;
    }

    void destroyAndPoison()
    {
        obj->~T();
        obj = nullptr;
        std::memset(buf, kPoison, sizeof(buf));
    }

    int breaches() const
    {
        int n = 0;
        for (size_t i = 0; i < sizeof(buf); i++)
            if (static_cast<unsigned char>(buf[i]) != kPoison) n++;
        return n;
    }

    /* Only for the self-test of the detector. */
    void scribble(size_t offset, unsigned char v) { buf[offset] = static_cast<char>(v); }

    size_t size() const { return sizeof(buf); }

private:
    alignas(T) char buf[sizeof(T)];
    T *obj = nullptr;
};

/* ---- probes ----------------------------------------------------------- */

/* The two switch IOs are abstract (readValue() is pure virtual); the probes
 * add nothing else - the code under test is the production emitChange(). */
class LongPressProbe: public InputSwitchLongPress
{
public:
    explicit LongPressProbe(Params &p): InputSwitchLongPress(p) {}
protected:
    bool readValue() override { return false; }
};

class TripleProbe: public InputSwitchTriple
{
public:
    explicit TripleProbe(Params &p): InputSwitchTriple(p) {}
protected:
    bool readValue() override { return false; }
};

/* KNXIo<Base> is a template mixin with a protected constructor: it has no
 * instance of its own, it is instantiated by the eleven KNXInput / KNXOutput
 * classes of IO/KNX/KNXIo.cpp. This probe is the same instantiation as the
 * production KNXInputSwitch (KNXIo.cpp:52), and it calls the very same
 * readAtStart() - the site under test is KNXIo.h, once, for all eleven. */
class KnxSwitchProbe: public KNXIo<InputSwitch>
{
public:
    explicit KnxSwitchProbe(Params &p):
        KNXIo<InputSwitch>(p, "KnxSwitchProbe", "T3.40 probe")
    {
        readAtStart(KNXValue::EIS_Switch_OnOff);
    }
protected:
    bool readValue() override { return false; }
};

/* ---- parameters ------------------------------------------------------- */

Params switchParams(const std::string &id)
{
    Params p;
    p.Add("id", id);
    p.Add("name", id);
    p.Add("enabled", "true");
    p.Add("visible", "false");
    return p;
}

Params scenarioParams(const std::string &id)
{
    Params p;
    p.Add("type", "scenario");
    p.Add("id", id);
    p.Add("name", id);
    p.Add("enabled", "true");
    p.Add("visible", "false");
    //Scenario defaults log_history to true, which sends the activation event
    //through HistLogger and its sqlite file. Nothing here is about history,
    //and a core suite must leave no database behind: turn it off explicitly.
    p.Add("log_history", "false");
    return p;
}

Params knxParams(const std::string &id, const char *readAtStart,
                 const std::string &host = "127.0.0.1")
{
    Params p;
    p.Add("type", "KNXInputSwitch");
    p.Add("id", id);
    p.Add("name", id);
    p.Add("knx_group", "0/1/2");
    p.Add("host", host);
    p.Add("read_at_start", readAtStart);
    p.Add("enabled", "true");
    p.Add("visible", "false");
    return p;
}

Params roonParams(const std::string &id)
{
    Params p;
    p.Add("type", "Roon");
    p.Add("id", id);
    p.Add("name", id);
    p.Add("zone_id", "160132a7337c26b01e556c2809514e65d6a0");
    p.Add("host", "192.168.7.42");
    p.Add("port", "9331");
    p.Add("enabled", "true");
    p.Add("visible", "false");
    return p;
}

/* The reset one-shot of the three switch-like IOs. Non-zero values only: 0 is
 * what the callback writes, so a case asking for 0 would be green both ways.
 * kInsideWindowMs is strictly below the delay, kPastWindowMs comfortably
 * above it. */
const int kResetDelayMs = 250;
const int kInsideWindowMs = 90;
const int kPastWindowMs = 650;

/* KNXIo::readAtStart() uses 1.5 s - the widest window of the whole sweep. */
const int kKnxPastWindowMs = 1900;

/* ExternProcServer defers processExited.emit() by 100 ms (ExternProc.cpp). */
const int kExternProcInsideWindowMs = 40;
const int kExternProcPastWindowMs = 400;

/* A path uv_spawn cannot resolve, so the spawn fails SYNCHRONOUSLY and leaves
 * no child behind. */
const char *const kNoSuchBinary = "/nonexistent/calaos_t340_no_such_binary";

/* How many unix sockets THIS process has bound, i.e. how many ExternProcServer
 * are alive. The constructor binds /tmp/calaos_proc_<uuid>_<name>_<pid> and
 * only the destructor unlinks it (IO/ExternProc.cpp), and our own pid is in
 * the name, so a concurrent `make check -jN` peer cannot be miscounted.
 * Used as the observable of the KNX read_at_start one-shot: see there. */
int countOwnSockets()
{
    const std::string suffix = "_" + std::to_string(static_cast<long>(::getpid()));
    const std::string prefix = "calaos_proc_";
    int n = 0;

    DIR *d = ::opendir("/tmp");
    if (!d)
        return -1;

    for (struct dirent *e = ::readdir(d); e; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name.size() <= prefix.size() + suffix.size())
            continue;
        if (name.compare(0, prefix.size(), prefix) != 0)
            continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        n++;
    }

    ::closedir(d);
    return n;
}

}

/******************************************************************************
 * ⭐ THE GUARD THAT IS *ABSENT* - review reserve 1
 *
 * LifetimeTag makes a BADLY PLACED guard unwritable at a call site (the token
 * only ever reaches the callback as a weak_ptr; MU-F is the mutant that proves
 * the remaining hole is inside LifetimeTag itself). It does nothing at all
 * about a guard that is simply NOT THERE: Timer::singleShot() and
 * Idler::singleIdler() are still public, and a new call site written tomorrow
 * with a raw `this` compiles, links and runs exactly like the twenty this
 * ticket weighed.
 *
 * Nothing in the tree detected that. The three alternatives were weighed:
 *
 *   - [[deprecated]] on Timer::singleShot(): warns on the FOUR call sites this
 *     ticket deliberately left raw, on the eight already carrying a guard of
 *     their own, on the six sigc::mem_fun of Squeezebox - and inside
 *     LifetimeTag, which calls it. 25 warnings for 0 defects, and the tree
 *     builds with -Werror nowhere, so the noise would simply be ignored.
 *   - an overload taking the token, required for IOBase subclasses: C++ cannot
 *     express "this call site is inside a class deriving from X"; the check
 *     would have to be a name, i.e. exactly the discipline that failed.
 *   - THIS: a census of the raw call sites, frozen at the value this ticket
 *     measured. A new raw one-shot anywhere under src/ fails the case, and the
 *     fix is either LifetimeTag or one line of allowlist plus the reason.
 *
 * ⚠️ What it is NOT: it does not know whether a call site captures `this`, and
 * it never will. It says "somebody added a fire-and-forget one-shot that this
 * ticket never looked at" - which is the question that was unanswerable
 * before, and the only one a text census can answer honestly.
 ******************************************************************************/

namespace SourceCensus
{

/* Comments blanked, string literals preserved. The tree MENTIONS
 * Timer::singleShot in comments far more often than it calls it (this file
 * included), so a census that does not whiten comments counts prose. */
inline std::string whitenComments(const std::string &s)
{
    std::string out;
    out.reserve(s.size());

    enum State { Code, LineComment, BlockComment, Str, Chr };
    State st = Code;

    for (size_t i = 0; i < s.size(); i++)
    {
        const char c = s[i];
        const char n = (i + 1 < s.size()) ? s[i + 1] : '\0';

        switch (st)
        {
        case Code:
            if (c == '/' && n == '/') { st = LineComment;  out += "  "; i++; break; }
            if (c == '/' && n == '*') { st = BlockComment; out += "  "; i++; break; }
            if (c == '"') st = Str;
            else if (c == '\'') st = Chr;
            out += c;
            break;

        case LineComment:
            if (c == '\n') { st = Code; out += '\n'; }
            else out += ' ';
            break;

        case BlockComment:
            if (c == '*' && n == '/') { st = Code; out += "  "; i++; break; }
            out += (c == '\n') ? '\n' : ' ';
            break;

        case Str:
        case Chr:
            if (c == '\\' && i + 1 < s.size()) { out += c; out += n; i++; break; }
            if ((st == Str && c == '"') || (st == Chr && c == '\'')) st = Code;
            out += c;
            break;
        }
    }

    return out;
}

/* The census counts, it never locates, so every needle below is matched on the
 * text with ALL whitespace removed: `Timer :: singleShot (` and
 * `Timer::singleShot(` are the same call site, and a line break inside the
 * expression cannot hide one. */
inline std::string squeezeSpace(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    return out;
}

inline bool hasSuffix(const std::string &s, const std::string &suf)
{
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

/* Every C++ source under `dir`, recursively.
 *
 * ⚠️ src/lib/uvw is skipped: it is a VENDORED third party carrying its own
 * event loop and its own test suite (106 loop->run() of its own). Nothing we
 * write lives there, and counting it would drown both censuses. */
inline void collectSources(const std::string &dir, std::vector<std::string> &out)
{
    DIR *d = ::opendir(dir.c_str());
    if (!d)
        return;

    for (struct dirent *e = ::readdir(d); e; e = ::readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == "..")
            continue;

        const std::string path = dir + "/" + name;

        struct stat st;
        if (::stat(path.c_str(), &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            if (name == "uvw")
                continue;
            collectSources(path, out);
        }
        else if (hasSuffix(name, ".cpp") || hasSuffix(name, ".h") ||
                 hasSuffix(name, ".hpp") || hasSuffix(name, ".cc"))
        {
            out.push_back(path);
        }
    }

    ::closedir(d);
}

//path (relative to src/) -> number of occurrences, files with 0 omitted
inline std::map<std::string, int> census(const std::vector<std::string> &needles,
                                         int *filesScanned)
{
    const std::string root = CALAOS_SRC_DIR;

    std::vector<std::string> files;
    collectSources(root, files);
    if (filesScanned)
        *filesScanned = static_cast<int>(files.size());

    std::map<std::string, int> hits;

    for (const std::string &p : files)
    {
        std::ifstream f(p.c_str(), std::ios::in | std::ios::binary);
        if (!f.is_open())
            continue;

        std::ostringstream buf;
        buf << f.rdbuf();

        const std::string text = squeezeSpace(whitenComments(buf.str()));

        int n = 0;
        for (const std::string &needle : needles)
        {
            for (size_t i = text.find(needle); i != std::string::npos;
                 i = text.find(needle, i + needle.size()))
                n++;
        }

        if (n > 0)
        {
            std::string rel = p;
            if (rel.compare(0, root.size(), root) == 0)
                rel.erase(0, root.size() + 1);
            hits[rel] = n;
        }
    }

    return hits;
}

/* Reports every file whose count exceeds what the ticket weighed, and every
 * file the ticket never saw. Empty means the census is within the allowance. */
inline std::string overAllowance(const std::map<std::string, int> &found,
                                 const std::map<std::string, int> &allowed)
{
    std::ostringstream out;

    for (const auto &kv : found)
    {
        auto it = allowed.find(kv.first);
        if (it == allowed.end())
            out << "\n  NEW FILE  " << kv.first << " : " << kv.second;
        else if (kv.second > it->second)
            out << "\n  MORE THAN WEIGHED  " << kv.first << " : "
                << kv.second << " > " << it->second;
    }

    return out.str();
}

/* The 25 raw call sites of src/ plus the 4 declarations/definitions/wrappers
 * of Timer.h and Timer.cpp, exactly as measured on this branch. NOT a list of
 * unguarded sites: eight of them carry a hand written guard of their own and
 * four were weighed and deliberately left raw (see T3.40.md §8). It is the
 * list of one-shots this ticket LOOKED AT. */
inline std::map<std::string, int> weighedOneShotSites()
{
    return {
        //src/lib
        { "lib/Timer.cpp",                        2 }, //the two definitions
        { "lib/Timer.h",                          2 }, //LifetimeTag's own two calls
        { "lib/UrlDownloader.cpp",                3 }, //:122/:169 serial guard, :739 deliberate `delete this`
        //src/bin/calaos_server
        { "bin/calaos_server/main.cpp",           1 }, //mem_fun on ListeRoom::Instance(), no `this`
        { "bin/calaos_server/CalaosConfig.cpp",   1 }, //function local static singleton, no delete in the tree
        { "bin/calaos_server/EventManager.cpp",   1 }, //idem
        { "bin/calaos_server/McpServerManager.cpp", 1 }, //idem
        { "bin/calaos_server/JsonApiHandlerHttp.cpp", 1 },      //already guarded
        { "bin/calaos_server/RemoteUI/RemoteUIWebSocketHandler.cpp", 1 }, //already guarded
        { "bin/calaos_server/Audio/AVRRose.cpp",  1 },          //already guarded (wtag)
        { "bin/calaos_server/Audio/AVReceiver.cpp", 1 },        //already guarded (wtag)
        { "bin/calaos_server/Audio/Squeezebox.cpp", 6 },        //sigc::trackable + mem_fun
        { "bin/calaos_server/IPCam/Foscam.cpp",   1 },          //[=] that does NOT capture this
        { "bin/calaos_server/LuaScript/ScriptExec.cpp", 1 },    //idem
        { "bin/calaos_server/IO/OutputShutter.cpp", 2 },        //T3.34's own guard
        { "bin/calaos_server/IO/OutputShutterSmart.cpp", 2 },   //T3.34's own guard
        { "bin/calaos_server/IO/Gpio/GpioCtrl.cpp", 1 },        //already guarded
        { "bin/calaos_server/IO/Reolink/ReolinkInputSwitch.cpp", 1 }, //already guarded
    };
}

} //namespace SourceCensus

/******************************************************************************
 * The source guard. Declared FIRST on purpose: it reads files and touches
 * neither the loop nor an IO, so it still reports even when a mutation makes a
 * later suite kill the binary.
 ******************************************************************************/

//Without this, both censuses below could be green because the scanner is
//broken rather than because the tree is clean.
TEST(IoLifetimeSourceGuardTest, TheScannerCountsCallsAndNotProse)
{
    const std::string sample =
        "void f()\n"
        "{\n"
        "    //Timer::singleShot(1, x); mentioned in a line comment\n"
        "    /* Timer::singleShot(2, x); mentioned in a block comment */\n"
        "    const char *s = \"Timer::singleShot(3, x); inside a literal\";\n"
        "    Timer :: singleShot ( 4, x );\n"
        "    Idler::singleIdler(y);\n"
        "}\n";

    const std::string text =
        SourceCensus::squeezeSpace(SourceCensus::whitenComments(sample));

    EXPECT_EQ(text.find("Timer::singleShot(1"), std::string::npos)
        << "a line comment was counted as a call";
    EXPECT_EQ(text.find("Timer::singleShot(2"), std::string::npos)
        << "a block comment was counted as a call";
    EXPECT_NE(text.find("Timer::singleShot(3"), std::string::npos)
        << "a string literal was whitened: the scanner damages code it must read";

    EXPECT_NE(text.find("Timer::singleShot(4"), std::string::npos)
        << "a call spelled with spaces around :: was missed";
    EXPECT_NE(text.find("Idler::singleIdler("), std::string::npos)
        << "the idler needle never matches";
}

TEST(IoLifetimeSourceGuardTest, NoRawOneShotCallSiteThisTicketNeverWeighed)
{
    int scanned = 0;
    const std::map<std::string, int> found =
        SourceCensus::census({ "Timer::singleShot(", "Idler::singleIdler(" }, &scanned);

    //Non vacuity: an unreadable or empty tree must be RED, never green.
    ASSERT_GE(scanned, 300) << "only " << scanned << " sources found under "
                            << CALAOS_SRC_DIR << ": the census read nothing";

    int total = 0;
    for (const auto &kv : found) total += kv.second;
    ASSERT_GE(total, 20) << "the census found " << total
                         << " one-shot call sites in a tree that has at least 29";

    const std::string extra =
        SourceCensus::overAllowance(found, SourceCensus::weighedOneShotSites());

    EXPECT_TRUE(extra.empty())
        << "a fire-and-forget one-shot was added where T3.40 never looked at "
           "one." << extra << "\n\n"
        << "Timer::singleShot()/Idler::singleIdler() copy the slot into an "
           "ANONYMOUS uvw handle: nothing can cancel it, so a slot holding "
           "`this` outlives its object (FINDINGS.md / F-SIGC-1). If the new "
           "site captures `this`, arm it through a LifetimeTag instead - "
           "IOBase already carries one, `ioAlive`. If it does not, add it to "
           "SourceCensus::weighedOneShotSites() WITH THE REASON.";
}

/******************************************************************************
 * ⭐ THE TOKEN DIES LAST - review reserve 2
 *
 * IOBase::ioAlive is a member of the BASE, so it is destroyed after the body
 * of ~Derived and after every derived member. No declaration order can change
 * that: base members always outlive derived ones. During the whole derived
 * teardown the guard therefore answers "alive", and a one-shot that fired
 * right then would run on a half destroyed object.
 *
 * ⭐ THE ONLY THING THAT CLOSES THAT WINDOW IS AN INVARIANT, AND HERE IT IS:
 *
 *      NO DESTRUCTOR IN THIS TREE PUMPS THE EVENT LOOP.
 *
 * A pending one-shot cannot fire while a destructor runs unless something
 * inside that destructor gives the loop a turn. This case is that invariant,
 * written down and measured: the whole of src/ (minus the vendored uvw) pumps
 * the loop in exactly THREE places, none of them a destructor -
 * calaos_server/main.cpp (the server main loop) and the two requestUrl()
 * bindings of LuaScript/ScriptBindings.cpp, which run inside the script SIDE
 * PROCESS.
 *
 * ⚠️ Adding a fourth is not forbidden - it is a question this case forces
 * somebody to answer: is the new one reachable from a destructor? Add it to
 * the map below WITH THAT ANSWER, or the window reopens silently.
 ******************************************************************************/
TEST(IoLifetimeSourceGuardTest, NothingNewPumpsTheEventLoop)
{
    int scanned = 0;
    const std::map<std::string, int> found =
        SourceCensus::census({ "->run(", "->run<", "uv_run(" }, &scanned);

    ASSERT_GE(scanned, 300) << "only " << scanned << " sources found under "
                            << CALAOS_SRC_DIR << ": the census read nothing";

    const std::map<std::string, int> expected = {
        //the server main loop - runs until SIGTERM, not a destructor
        { "bin/calaos_server/main.cpp", 1 },
        //requestUrl()/requestUrlPost(): a nested run() inside the LuaScript
        //SIDE PROCESS, waiting for one download. Not a destructor either.
        { "bin/calaos_server/LuaScript/ScriptBindings.cpp", 2 },
    };

    EXPECT_EQ(found, expected)
        << "the set of places that pump the event loop changed.\n"
           "IOBase::ioAlive is destroyed AFTER the whole derived object, so "
           "the lifetime guard answers `alive` for the entire duration of a "
           "destructor. That is harmless only because no destructor gives the "
           "loop a turn. Whoever added or moved a run() must answer whether "
           "the new one can be reached from a destructor, then update this "
           "map with the answer.";
}

/******************************************************************************
 * The oracle itself
 ******************************************************************************/

class IoLifetimeTest: public CoreFixture
{
};

//Without this, every poison case in this file would be vacuously green.
TEST_F(IoLifetimeTest, PoisonDetectorSeesAWriteIntoTheFreedStorage)
{
    loadConfig();

    Params p = switchParams("t340_detector_selftest");
    OwnedIoStorage<LongPressProbe> store;
    LongPressProbe *io = store.construct(p);
    ASSERT_TRUE(io != nullptr);

    store.destroyAndPoison();
    ASSERT_EQ(store.breaches(), 0) << "the poison did not take";

    //One byte, written by hand, standing in for the dangling `this` write.
    store.scribble(0, 0x00);
    EXPECT_EQ(store.breaches(), 1)
        << "the detector cannot see a write into the freed storage";
}

/******************************************************************************
 * IO/InputSwitchLongPress.cpp:88 - ioAlive.singleShot(0.250, [this]{ value = 0; })
 ******************************************************************************/

TEST_F(IoLifetimeTest, LongPressResetStillRunsWhileTheIoIsAlive)
{
    loadConfig();

    Params p = switchParams("t340_longpress_alive");
    LongPressProbe io(p);

    ASSERT_TRUE(io.set_value(1.));
    ASSERT_EQ(io.get_value_double(), 1.);

    //Well inside the 250 ms: the window this defect needs really exists.
    pumpLoopFor(kInsideWindowMs);
    EXPECT_EQ(io.get_value_double(), 1.)
        << "the reset fired before " << kInsideWindowMs << " ms: there is no "
           "window left for a deletion to slip into";

    pumpLoopFor(kPastWindowMs - kInsideWindowMs);
    EXPECT_EQ(io.get_value_double(), 0.)
        << "the reset one-shot never ran at all on a LIVE IO: the cases below "
           "would then be green for the wrong reason";
}

TEST_F(IoLifetimeTest, LongPressResetDoesNotOutliveTheDeletedIo)
{
    loadConfig();

    Params p = switchParams("t340_longpress_lifetime");
    OwnedIoStorage<LongPressProbe> store;
    LongPressProbe *io = store.construct(p);

    ASSERT_TRUE(io->set_value(1.));
    ASSERT_EQ(io->get_value_double(), 1.);

    store.destroyAndPoison();
    pumpLoopFor(kPastWindowMs);

    EXPECT_EQ(store.breaches(), 0)
        << "the reset one-shot of InputSwitchLongPress wrote into a destroyed "
           "IO (" << store.breaches() << " of " << store.size()
        << " bytes of the freed storage are no longer poison)";
}

/******************************************************************************
 * IO/InputSwitchTriple.cpp:100 - ioAlive.singleShot(0.250, [this]{ resetInput(); })
 *
 * The one mem_fun of the five: sigc++ binds a RAW pointer here, because
 * InputSwitchTriple is not a sigc::trackable. This is the only site of the
 * whole sweep that deriving IOBase from sigc::trackable would have covered.
 ******************************************************************************/

TEST_F(IoLifetimeTest, TripleResetStillRunsWhileTheIoIsAlive)
{
    loadConfig();

    Params p = switchParams("t340_triple_alive");
    TripleProbe io(p);

    ASSERT_TRUE(io.set_value(2.));
    ASSERT_EQ(io.get_value_double(), 2.);

    pumpLoopFor(kInsideWindowMs);
    EXPECT_EQ(io.get_value_double(), 2.)
        << "the reset fired before " << kInsideWindowMs << " ms";

    pumpLoopFor(kPastWindowMs - kInsideWindowMs);
    EXPECT_EQ(io.get_value_double(), 0.)
        << "the reset one-shot never ran at all on a LIVE IO";
}

TEST_F(IoLifetimeTest, TripleResetDoesNotOutliveTheDeletedIo)
{
    loadConfig();

    Params p = switchParams("t340_triple_lifetime");
    OwnedIoStorage<TripleProbe> store;
    TripleProbe *io = store.construct(p);

    ASSERT_TRUE(io->set_value(2.));
    ASSERT_EQ(io->get_value_double(), 2.);

    store.destroyAndPoison();
    pumpLoopFor(kPastWindowMs);

    EXPECT_EQ(store.breaches(), 0)
        << "InputSwitchTriple::resetInput() ran on a destroyed IO ("
        << store.breaches() << " of " << store.size()
        << " bytes of the freed storage are no longer poison)";
}

/******************************************************************************
 * IO/Scenario.cpp:105 - ioAlive.singleShot(0.250, [this]{ value = false; })
 ******************************************************************************/

TEST_F(IoLifetimeTest, ScenarioResetStillRunsWhileTheIoIsAlive)
{
    loadConfig();

    Params p = scenarioParams("t340_scenario_alive");
    Scenario io(p);

    ASSERT_TRUE(io.set_value(true));
    ASSERT_TRUE(io.get_value_bool());

    pumpLoopFor(kInsideWindowMs);
    EXPECT_TRUE(io.get_value_bool())
        << "the reset fired before " << kInsideWindowMs << " ms";

    pumpLoopFor(kPastWindowMs - kInsideWindowMs);
    EXPECT_FALSE(io.get_value_bool())
        << "the reset one-shot never ran at all on a LIVE IO";
}

TEST_F(IoLifetimeTest, ScenarioResetDoesNotOutliveTheDeletedIo)
{
    loadConfig();

    Params p = scenarioParams("t340_scenario_lifetime");
    OwnedIoStorage<Scenario> store;
    Scenario *io = store.construct(p);

    ASSERT_TRUE(io->set_value(true));
    ASSERT_TRUE(io->get_value_bool());

    store.destroyAndPoison();
    pumpLoopFor(kPastWindowMs);

    EXPECT_EQ(store.breaches(), 0)
        << "the reset one-shot of Scenario wrote into a destroyed IO ("
        << store.breaches() << " of " << store.size()
        << " bytes of the freed storage are no longer poison)";
}

/******************************************************************************
 * Audio/RoonPlayer.cpp - eight one-shots, all [this] lambdas
 *
 * ⚠️ RoonPlayer IS an IOBase: RoonPlayer -> AudioPlayer -> IOBase
 * (Audio/RoonPlayer.h:167, Audio/AudioPlayer.h:32). It is registered with
 * REGISTER_IO_USERTYPE(Roon, RoonPlayer), so ListeRoom::deleteIO() reaches it
 * exactly like a shutter. That it ALSO derives from sigc::trackable changes
 * nothing: all eight sites are lambdas, and trackable never disconnects one.
 *
 * The site used here is get_playlist_size (the one-shot calls
 * get_playlist_size_cb, whose body touches NO member): after the player is
 * gone the callback does not crash, it quietly runs and calls back into the
 * caller's slot. That makes it the sharpest observable of the file - a
 * counter, not a memory pattern - and it shows the defect is not only a crash
 * risk: a destroyed IO answers an API request.
 *
 * Declared after the poison suite: building a RoonPlayer starts RoonCtrl,
 * whose failed spawn leaves respawn timers on the loop for the rest of the
 * process.
 ******************************************************************************/

class IoLifetimeRoonTest: public CoreFixture
{
};

TEST_F(IoLifetimeRoonTest, PlaylistSizeAnswersWhileThePlayerIsAlive)
{
    loadConfig();

    Params p = roonParams("t340_roon_alive");
    RoonPlayer player(p);

    int answers = 0;
    player.get_playlist_size([&answers](AudioPlayerData) { answers++; });

    ASSERT_EQ(answers, 0) << "the one-shot ran synchronously, there is no window";

    pumpLoopFor(200);
    EXPECT_EQ(answers, 1)
        << "the deferred answer never ran at all on a LIVE player: the case "
           "below would then be green for the wrong reason";
}

TEST_F(IoLifetimeRoonTest, PlaylistSizeDoesNotAnswerForADeletedPlayer)
{
    loadConfig();

    Params p = roonParams("t340_roon_lifetime");
    OwnedIoStorage<RoonPlayer> store;
    RoonPlayer *player = store.construct(p);

    int answers = 0;
    player->get_playlist_size([&answers](AudioPlayerData) { answers++; });
    ASSERT_EQ(answers, 0);

    store.destroyAndPoison();
    pumpLoopFor(200);

    EXPECT_EQ(answers, 0)
        << "a destroyed RoonPlayer answered an API request from the loop";
    EXPECT_EQ(store.breaches(), 0)
        << "the deferred answer wrote into the destroyed player";
}

/******************************************************************************
 * IO/ExternProc.cpp:196 and :205 - alive.singleShot(0.1, [this]{ ... })
 *
 * ⭐ THE ONE OF THE THREE GUARDS WITHOUT AN ORACLE THAT ACTUALLY NEEDED ONE.
 * T3.40 closed sixteen sites and left three of them unexercised
 * (IPCam/IPCam.cpp:126, this one, PollListenner.cpp:60). This is the only one
 * of the three where the object is destroyed INSIDE the 100 ms window, by
 * production code, on an ordinary path: ~WagoMap, ~KNXCtrl, ~OLACtrl, ~OWCtrl
 * and ~RoonPlayer all `delete process`, and LuaScript/ScriptExec.cpp:132
 * deletes the server from an idler that processExited itself armed. The other
 * two rest on a code reading; this one now rests on a measurement.
 *
 * HOW THE WINDOW IS OPENED HERE: uv_spawn on a path that does not exist fails
 * SYNCHRONOUSLY (ExternProc.cpp checks hasFailedStarting on the line after
 * spawn()), so ProcessHandle publishes ErrorEvent inside startProcess(), and
 * the handler arms the 100 ms deferred processExited.emit(). No child is
 * created, nothing is left to reap.
 *
 * ⚠️ ExternProcServer is NOT an IOBase: it carries its own LifetimeTag
 * (`alive`, ExternProc.h). Deriving IOBase from sigc::trackable would not have
 * covered it either way - and it IS a sigc::trackable already, which is
 * exactly the trap: trackable disconnects mem_fun, never a lambda.
 *
 * ⚠️ DECLARED AFTER THE ROON SUITE, and that is not cosmetic: the deferred
 * callback here is processExited.emit(), i.e. an immediate dereference of the
 * dangling `this`, so without the guard this case does not fail, it KILLS the
 * binary. Measured: with the suite declared earlier, the badly placed guard
 * mutant (MU-F) died HERE and the Roon oracle never got to run, losing one of
 * the four red cases that make MU-F attributable. Non fatal oracles first.
 ******************************************************************************/

class IoLifetimeExternProcTest: public CoreFixture
{
};

TEST_F(IoLifetimeExternProcTest, ProcessExitedStillFiresWhileTheServerIsAlive)
{
    loadConfig();

    ExternProcServer srv("t340probe");

    int exited = 0;
    srv.processExited.connect([&exited]() { exited++; });

    srv.startProcess(kNoSuchBinary, "t340", "");
    ASSERT_EQ(exited, 0)
        << "processExited was emitted synchronously: there is no window";

    pumpLoopFor(kExternProcInsideWindowMs);
    EXPECT_EQ(exited, 0)
        << "the deferred processExited fired before "
        << kExternProcInsideWindowMs << " ms: no window left for a deletion";

    pumpLoopFor(kExternProcPastWindowMs - kExternProcInsideWindowMs);
    EXPECT_EQ(exited, 1)
        << "the deferred processExited never ran at all on a LIVE server: the "
           "case below would then be green for the wrong reason";
}

TEST_F(IoLifetimeExternProcTest, ProcessExitedDoesNotOutliveTheDeletedServer)
{
    loadConfig();

    OwnedIoStorage<ExternProcServer> store;
    ExternProcServer *srv = store.construct(std::string("t340probe"));

    int exited = 0;
    srv->processExited.connect([&exited]() { exited++; });

    srv->startProcess(kNoSuchBinary, "t340", "");
    ASSERT_EQ(exited, 0);

    store.destroyAndPoison();
    pumpLoopFor(kExternProcPastWindowMs);

    EXPECT_EQ(exited, 0)
        << "a destroyed ExternProcServer emitted processExited from the loop";
    EXPECT_EQ(store.breaches(), 0)
        << "the deferred processExited touched the destroyed server ("
        << store.breaches() << " of " << store.size()
        << " bytes of the freed storage are no longer poison)";
}

/******************************************************************************
 * IO/KNX/KNXIo.h:78 - ioAlive.singleShot(1.5, [this, eis, group_bases]{ ... })
 *
 * The widest window of the sweep (1.5 s), opened at CONSTRUCTION whenever
 * read_at_start is set, and shared by the eleven KNX IO types through the
 * KNXIo<Base> mixin.
 *
 * ⚠️ DECLARED LAST. Its callback dereferences the dangling `this` right away
 * (ctrl() reads this->param, knxBase is a member), so before the guard this
 * case does not fail, it KILLS the binary: exit 139 and NOT ONE FAILED line.
 * That is the signal to read - the exit code, plus which guard was removed -
 * never the log.
 ******************************************************************************/

class IoLifetimeKnxTest: public CoreFixture
{
};

/* ⭐ The companion the review asked for. The three switch IOs and RoonPlayer
 * each have a *StillRunsWhileTheIoIsAlive case; KNX did not, so its
 * non-vacuity rested entirely on MU-E - on "removing the guard kills the
 * binary". That proves the guard is load bearing TODAY; it says nothing the
 * day read_at_start stops arming anything at all. On that day MU-E would
 * simply stop crashing, the mutant would survive, and
 * ReadAtStartDoesNotOutliveTheDeletedIo would stay green for the wrong reason
 * - which is exactly what a counter-mutation campaign cannot see from inside.
 *
 * THE OBSERVABLE, and why it is this one. The callback ends on
 * KNXCtrl::readValue() -> process->sendMessage(), on a server with no client
 * connected: it writes nothing, returns nothing and changes no member. What it
 * DOES do is call ctrl(), i.e. KNXCtrl::Instance(host) - and KNXCtrl keeps one
 * instance per host and builds two ExternProcServer in its constructor
 * ("knx" and "knx_monitor"), each binding a unix socket in /tmp. So on a host
 * NO OTHER CASE IN THIS BINARY USES, the socket count of this process can only
 * grow if the read_at_start one-shot actually fired.
 */
TEST_F(IoLifetimeKnxTest, ReadAtStartStillRunsWhileTheIoIsAlive)
{
    loadConfig();

    const int before = countOwnSockets();
    ASSERT_GE(before, 0) << "/tmp could not be read: the observable is blind";

    Params p = knxParams("t340_knx_alive", "true", "127.0.0.9");
    KnxSwitchProbe io(p);

    ASSERT_EQ(countOwnSockets(), before)
        << "building the IO already built the KNX controller for this host: "
           "the observable can no longer tell the one-shot from the constructor";

    //Well inside the 1.5 s: the widest window of the sweep really exists.
    pumpLoopFor(kInsideWindowMs);
    EXPECT_EQ(countOwnSockets(), before)
        << "the read_at_start one-shot fired before " << kInsideWindowMs
        << " ms: there is no window left for a deletion to slip into";

    pumpLoopFor(kKnxPastWindowMs - kInsideWindowMs);
    EXPECT_GT(countOwnSockets(), before)
        << "the read_at_start one-shot never ran at all on a LIVE IO: the case "
           "below would then be green for the wrong reason, and MU-E would be "
           "the only thing keeping it honest";
}

TEST_F(IoLifetimeKnxTest, ReadAtStartDoesNotOutliveTheDeletedIo)
{
    loadConfig();

    //Bring KNXCtrl (and the respawn timers of its failed spawn) into
    //existence first, so that the case below only adds the read_at_start
    //one-shot to the loop.
    {
        Params warm = knxParams("t340_knx_warmup", "false");
        KnxSwitchProbe warmup(warm);
    }

    Params p = knxParams("t340_knx_lifetime", "true");
    OwnedIoStorage<KnxSwitchProbe> store;
    KnxSwitchProbe *io = store.construct(p);
    ASSERT_TRUE(io != nullptr);

    store.destroyAndPoison();
    pumpLoopFor(kKnxPastWindowMs);

    EXPECT_EQ(store.breaches(), 0)
        << "the read_at_start one-shot of KNXIo ran on a destroyed IO ("
        << store.breaches() << " of " << store.size()
        << " bytes of the freed storage are no longer poison)";
}

//Own main instead of gtest_main: skip static destructors, see file header.
//
//⚠️ Skipping them also skips ~ExternProcServer, which is what unlinks the unix
//socket it bound in /tmp (IO/ExternProc.cpp:114-115). RoonCtrl and KNXCtrl are
//built by the last two suites, so this binary must give those sockets back by
//hand, and reap whatever child the failed spawns left. Both helpers match on
//OUR pid, so a concurrent `make check -jN` cannot be robbed.
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    const int ret = RUN_ALL_TESTS();

    RoonSpawn::reapChildren(1000);
    RoonSpawn::removeOwnSockets();

    fflush(nullptr);
    _exit(ret);
}
