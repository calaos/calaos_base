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

/*******************************************************************************
 * T3.27 - THE RETURN CONTRACT of the calaos:* scriptable API.
 *
 * setIOParam() and waitForIO() declare `return 1` - "I am leaving ONE result on
 * the lua stack" - and never push anything. Lunar::thunk (LuaScript/Lunar.h:130
 * -137) removes only `self` before dispatching, so THE CALL ARGUMENTS ARE STILL
 * ON THE STACK, and `return 1` hands the script back whatever sits on top of it:
 * its OWN LAST ARGUMENT. setIOParam() gives back `value`, waitForIO() gives back
 * `id`. Both are non-empty strings, both are truthy in lua, so
 *
 *     if calaos:waitForIO(io) then ... end
 *
 * takes the true branch UNCONDITIONALLY. A guard that guards nothing.
 *
 * ⭐ WHY THIS SUITE PUSHES A REAL lua_State AND WRITES REAL LUA.
 * The defect does not live in the C++ return value - `return 1` is a perfectly
 * ordinary integer. It lives in WHAT THE SCRIPT RECEIVES, which is a property of
 * the lua stack at the moment thunk() returns. Calling Lua_Calaos::setIOParam()
 * directly from C++ would freeze the integer 1 and see nothing at all. So every
 * case here goes through ScriptManager::ExecuteScript(), which is the ONLY place
 * in the tree that creates a lua_State, registers the class through
 * Lunar<Lua_Calaos>::Register() and runs a script - the same path calaos_script
 * takes in production (ScriptExtern_main.cpp).
 *
 * ⭐ AND WHY ONE CASE REPORTS THROUGH THE WIRE.
 * ExecuteScript() only hands back a boolean, so a script cannot tell the test
 * what it saw. WhatTheScriptActuallySeesComingBack works around that: it makes
 * the script send `type(r)` and `tostring(r)` BACK as set_param messages on the
 * ExternProcClient socket, which the fixture reads and decodes. The observed
 * type and value are printed, then asserted. That is the executed proof of the
 * paragraph above, not a restatement of it.
 *
 * ⚠️ FIXTURE VALUES ARE DELIBERATELY UNRELATED TO EACH OTHER.
 * The obvious way to write this suite and prove nothing is
 * `calaos:setIOParam(id, "k", true)`: `value` is ALREADY true, so the defect and
 * the fix hand back the same thing and every assertion stays green through both.
 * Here the io id, the param key and the param value are three different
 * non-empty strings, none of which is a boolean or nil, so `r == value`
 * separates the two worlds exactly.
 *
 * ⭐ THIS SUITE REACHES ScriptBindings.cpp. FINDINGS.md F-LUA-3 states that the
 * file "n'est lié dans AUCUN binaire de test de l'arbre" and concludes that the
 * io.set_param(key,value) permutation is out of reach. THAT IS NOT TRUE:
 * LuaSandbox_test already links ScriptBindings.$(OBJEXT), and so does this
 * suite. TheParamIsActuallyWrittenOnTheIo below reads the emitted frame and
 * turns that permutation RED. See the ticket sheet for the measurement.
 *
 * ⚠️ RELINK. Like 47 of the 84 suites, this one carries
 * _DEPENDENCIES = libcalaos_common.la, so changing ScriptBindings.cpp does NOT
 * relink the binary by itself (T3.36 in DECISIONS.md). Any mutation run against
 * it must rm -f both the .o and the test binary and check for the CXXLD line.
 ******************************************************************************/

#include "ScriptManager.h"
#include "ScriptWire.h"
#include "ExternProc.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

using namespace Calaos;

namespace
{

//Three values with nothing in common: the return of a binding can be compared
//to each one of them without ambiguity.
const std::string kIoId  = "io_probe_alpha";
const std::string kKey   = "probe_key_beta";
const std::string kValue = "sentinel-42-gamma";

//A concrete ExternProcClient: the base class is abstract, and LuaIOBase needs a
//non-null one or set_param() dereferences a null pointer (which would kill the
//binary with no FAILED line at all - one of the five false greens).
class ProbeClient: public ExternProcClient
{
public:
    ProbeClient(int &argc, char **&argv): ExternProcClient(argc, argv) {}

    bool setup(int &, char **&) override { return true; }
    int procMain() override { return 0; }

protected:
    void readTimeout() override {}
    void messageReceived(const std::string &) override {}
};

/*
 * The other end of the ExternProcClient socket, held open for the whole binary.
 *
 * ExternProcClient's constructor calls initLogger() and its destructor calls
 * Utils::freeLoggers(), which is process wide: building and tearing one down per
 * test would pull the logging facility out from under the rest of the suite. One
 * instance, created once, never destroyed.
 */
class WireProbe
{
public:
    static WireProbe &Instance()
    {
        static WireProbe *p = new WireProbe();
        return *p;
    }

    ExternProcClient *client() { return cli; }

    //Everything calaos_script emitted since the last call, decoded.
    std::vector<std::pair<std::string, Params>> drain()
    {
        std::vector<std::pair<std::string, Params>> msgs;

        if (srvfd < 0)
            return msgs;

        char buf[4096];
        ssize_t n;
        while ((n = ::recv(srvfd, buf, sizeof(buf), MSG_DONTWAIT)) > 0)
            rx.append(buf, static_cast<size_t>(n));

        //1 byte opcode, 4 bytes big endian length, payload. ExternProcMessage::
        //getRawData(), read back by hand so a framing change shows up here.
        size_t off = 0;
        while (rx.size() - off >= 5)
        {
            uint32_t len = (static_cast<uint8_t>(rx[off + 1]) << 24) |
                           (static_cast<uint8_t>(rx[off + 2]) << 16) |
                           (static_cast<uint8_t>(rx[off + 3]) << 8)  |
                            static_cast<uint8_t>(rx[off + 4]);
            if (rx.size() - off - 5 < len)
                break;

            std::string payload = rx.substr(off + 5, len);
            off += 5 + len;

            //Decoded with the shipped header, not with a copy of it: a change in
            //ScriptWire.h is visible from here.
            Json jroot;
            if (ScriptWire::parseMessage(payload, jroot))
            {
                Params p;
                const Json::const_iterator jd = jroot.find("data");
                if (jd != jroot.cend())
                    ScriptWire::decodeObject(*jd, p);
                msgs.push_back({ ScriptWire::stringGet(jroot, "msg"), p });
            }
        }
        rx.erase(0, off);

        return msgs;
    }

private:
    WireProbe()
    {
        //A listening AF_UNIX socket the client can actually connect to. Without
        //it sendMessage() writes to fd -1 and every emission is lost.
        char tmpl[] = "/tmp/calaos_luaapi_XXXXXX";
        sockpath = ::mkdtemp(tmpl);
        sockpath += "/sock";

        listenfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr;
        ::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        ::strncpy(addr.sun_path, sockpath.c_str(), sizeof(addr.sun_path) - 1);
        ::bind(listenfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
        ::listen(listenfd, 1);

        //ExternProcClient reads --socket out of argv and MUTATES argc/argv, so
        //both need real storage that outlives the call.
        static char a0[] = "LuaCalaosApi_test";
        static char a1[] = "--socket";
        static std::string pathstore;
        pathstore = sockpath;
        static char *argvstore[3];
        argvstore[0] = a0;
        argvstore[1] = a1;
        argvstore[2] = const_cast<char *>(pathstore.c_str());

        int argc = 3;
        char **argv = argvstore;
        cli = new ProbeClient(argc, argv);

        connected = cli->connectSocket();
        if (connected)
            srvfd = ::accept(listenfd, nullptr, nullptr);
    }

    std::string sockpath;
    std::string rx;
    int listenfd = -1;
    int srvfd = -1;
    bool connected = false;
    ProbeClient *cli = nullptr;
};

class LuaCalaosApi: public ::testing::Test
{
protected:
    void SetUp() override
    {
        Lua_Calaos &c = ScriptManager::Instance().luaCalaos;

        c.abort = false;
        c.ioMap.clear();

        LuaIOBase io(WireProbe::Instance().client());
        io.params.Add("id", kIoId);
        io.params.Add("var_type", "string");
        io.params.Add("state", "idle");
        c.ioMap[kIoId] = io;

        //waitForIO() spins on `while (!waitForIOChanged.emit(id) && !abort);`
        //under a ScriptWatchdogPause: a signal with no slot answers a default
        //constructed false and the loop never ends, watchdog paused. The slot
        //also records the id it was handed, which is the only observable of the
        //blocking half of the binding.
        emittedIds.clear();
        conn = c.waitForIOChanged.connect([](const std::string &id) -> bool
        {
            emittedIds.push_back(id);
            return true;
        });

        WireProbe::Instance().drain(); //discard anything left by a previous case
    }

    void TearDown() override
    {
        conn.disconnect();
        ScriptManager::Instance().luaCalaos.ioMap.clear();
    }

    static bool runScript(const std::string &s)
    {
        return ScriptManager::Instance().ExecuteScript(s);
    }

    static bool scriptFailed()
    {
        return ScriptManager::Instance().hasError();
    }

    static std::string lastError()
    {
        return ScriptManager::Instance().getErrorMsg();
    }

    static std::vector<std::string> emittedIds;
    sigc::connection conn;
};

std::vector<std::string> LuaCalaosApi::emittedIds;

} //namespace

/*******************************************************************************
 * ⭐ WHAT THE SCRIPT RECEIVES - the defect itself, observed from lua.
 ******************************************************************************/

/*
 * The script hands its own observation back over the wire, so this case prints
 * what a rule script really gets and then asserts the contract. Before the fix
 * it prints  type=string value=sentinel-42-gamma  for setIOParam and
 * type=string value=io_probe_alpha  for waitForIO, and fails on both EXPECT_EQ.
 */
TEST_F(LuaCalaosApi, WhatTheScriptActuallySeesComingBack)
{
    const std::string script =
        "local a = calaos:setIOParam(\"" + kIoId + "\", \"" + kKey + "\", \"" + kValue + "\")\n"
        "local b = calaos:waitForIO(\"" + kIoId + "\")\n"
        "calaos:setIOParam(\"" + kIoId + "\", \"observed_setIOParam\", type(a) .. \"/\" .. tostring(a))\n"
        "calaos:setIOParam(\"" + kIoId + "\", \"observed_waitForIO\", type(b) .. \"/\" .. tostring(b))\n"
        "return true\n";

    ASSERT_TRUE(runScript(script)) << lastError();
    ASSERT_FALSE(scriptFailed()) << lastError();

    std::string observedSet, observedWait;
    for (const auto &m : WireProbe::Instance().drain())
    {
        Params p = m.second;
        if (p["param"] == "observed_setIOParam") observedSet = p["value"];
        if (p["param"] == "observed_waitForIO")  observedWait = p["value"];
    }

    ASSERT_FALSE(observedSet.empty()) << "the probe frames never came back";
    ASSERT_FALSE(observedWait.empty()) << "the probe frames never came back";

    std::cout << "[ OBSERVED ] calaos:setIOParam(id, key, value) -> " << observedSet << std::endl;
    std::cout << "[ OBSERVED ] calaos:waitForIO(id)              -> " << observedWait << std::endl;

    //The contract: a setter and a blocking action leave nothing on the stack,
    //exactly like setIOValue() and requestUrl() next to them.
    EXPECT_EQ("nil/nil", observedSet)
            << "setIOParam() handed the script something; before T3.27 it handed back "
               "its own third argument";
    EXPECT_EQ("nil/nil", observedWait)
            << "waitForIO() handed the script something; before T3.27 it handed back "
               "its own io id";
}

//The two ⭐ cases the ticket names, each isolated so a mutation of one binding
//cannot be mistaken for a mutation of the other.
TEST_F(LuaCalaosApi, SetIOParamDoesNotHandBackItsThirdArgument)
{
    EXPECT_TRUE(runScript(
        "local r = calaos:setIOParam(\"" + kIoId + "\", \"" + kKey + "\", \"" + kValue + "\")\n"
        "return r ~= \"" + kValue + "\"\n")) << "setIOParam() gave the script back its own value";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

TEST_F(LuaCalaosApi, SetIOParamHandsBackNothingAtAll)
{
    EXPECT_TRUE(runScript(
        "local r = calaos:setIOParam(\"" + kIoId + "\", \"" + kKey + "\", \"" + kValue + "\")\n"
        "return r == nil\n")) << "setIOParam() left a value on the stack";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

TEST_F(LuaCalaosApi, WaitForIODoesNotHandBackItsIoId)
{
    EXPECT_TRUE(runScript(
        "local r = calaos:waitForIO(\"" + kIoId + "\")\n"
        "return r ~= \"" + kIoId + "\"\n")) << "waitForIO() gave the script back its own id";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

TEST_F(LuaCalaosApi, WaitForIOHandsBackNothingAtAll)
{
    EXPECT_TRUE(runScript(
        "local r = calaos:waitForIO(\"" + kIoId + "\")\n"
        "return r == nil\n")) << "waitForIO() left a value on the stack";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

/*
 * ⭐ THE USER VISIBLE SHAPE OF THE DEFECT, and the reason this ticket needs a
 * release note: the guard everyone writes.
 *
 * Before T3.27 this script returns true whatever happened, because `id` is a
 * non-empty string and every non-empty string is truthy in lua. After T3.27 the
 * return is nil and the SAME SCRIPT takes the else branch. A script written
 * against the old behaviour changes branch - that is the observable break.
 */
TEST_F(LuaCalaosApi, TheClassicGuardNoLongerReadsTrueByAccident)
{
    EXPECT_FALSE(runScript(
        "if calaos:waitForIO(\"" + kIoId + "\") then\n"
        "  return true\n"
        "else\n"
        "  return false\n"
        "end\n")) << "`if calaos:waitForIO(io) then` still reads true unconditionally";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

/*******************************************************************************
 * THE CONVENTION this ticket aligns on - the neighbours in the same method
 * table. These are green before AND after: they pin the rule the fix follows.
 ******************************************************************************/

//setIOValue() is setIOParam()'s own sibling and already returns 0.
TEST_F(LuaCalaosApi, TheSiblingSetterAlreadyHandsBackNothing)
{
    EXPECT_TRUE(runScript(
        "local r = calaos:setIOValue(\"" + kIoId + "\", \"" + kValue + "\")\n"
        "return r == nil\n")) << "setIOValue() no longer returns 0";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

//The getters do push, and what they push is the value, not an argument.
TEST_F(LuaCalaosApi, TheGettersStillHandBackTheirValue)
{
    EXPECT_TRUE(runScript(
        "local v = calaos:getIOParam(\"" + kIoId + "\", \"var_type\")\n"
        "if v ~= \"string\" then return false end\n"
        "local s = calaos:getIOValue(\"" + kIoId + "\")\n"
        "if s ~= \"idle\" then return false end\n"
        "return true\n")) << lastError();
    EXPECT_FALSE(scriptFailed()) << lastError();
}

/*******************************************************************************
 * ACQUIS - what must NOT move when the return contract changes.
 ******************************************************************************/

/*
 * ⭐ THE SIDE EFFECT, read off the wire.
 *
 * This is also the case F-LUA-3 declared unreachable. It decodes the emitted
 * set_param frame, so io.set_param(value, key) - the permutation the named types
 * of ScriptWire.h cannot catch, because the call site builds both wrappers
 * itself - lands the value under "param" and the key under "value", and this
 * turns RED.
 */
TEST_F(LuaCalaosApi, TheParamIsActuallyWrittenOnTheIo)
{
    ASSERT_TRUE(runScript(
        "calaos:setIOParam(\"" + kIoId + "\", \"" + kKey + "\", \"" + kValue + "\")\n"
        "return true\n")) << lastError();

    auto msgs = WireProbe::Instance().drain();
    ASSERT_EQ(1u, msgs.size()) << "expected exactly one message on the wire";
    EXPECT_EQ("set_param", msgs[0].first);

    Params p = msgs[0].second;
    EXPECT_EQ(kIoId,  p["id"]);
    EXPECT_EQ(kKey,   p["param"]);
    EXPECT_EQ(kValue, p["value"]);
}

//The failure paths go through lua_error(), which longjmps: they never were a
//return value and the fix does not touch them.
TEST_F(LuaCalaosApi, AnInvalidIoIdStillRaises)
{
    EXPECT_TRUE(runScript(
        "local ok, err = pcall(function()\n"
        "  calaos:setIOParam(\"no_such_io\", \"" + kKey + "\", \"" + kValue + "\")\n"
        "end)\n"
        "if ok then return false end\n"
        "return string.find(tostring(err), \"invalid IO id\") ~= nil\n"))
            << "setIOParam() on an unknown io no longer raises";
    EXPECT_FALSE(scriptFailed()) << lastError();

    EXPECT_TRUE(runScript(
        "local ok, err = pcall(function()\n"
        "  calaos:waitForIO(\"no_such_io\")\n"
        "end)\n"
        "if ok then return false end\n"
        "return string.find(tostring(err), \"invalid IO id\") ~= nil\n"))
            << "waitForIO() on an unknown io no longer raises";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

TEST_F(LuaCalaosApi, AWrongArityStillRaises)
{
    EXPECT_TRUE(runScript(
        "local ok = pcall(function() calaos:setIOParam(\"" + kIoId + "\") end)\n"
        "if ok then return false end\n"
        "ok = pcall(function() calaos:waitForIO() end)\n"
        "return not ok\n")) << "the arity guards no longer raise";
    EXPECT_FALSE(scriptFailed()) << lastError();
}

//waitForIO() blocks on the signal, and it must hand it the id it was called
//with. Emitting anything else would wait on the wrong io forever in production.
TEST_F(LuaCalaosApi, WaitForIOAsksTheSignalAboutTheIoItWasGiven)
{
    ASSERT_TRUE(runScript(
        "calaos:waitForIO(\"" + kIoId + "\")\n"
        "return true\n")) << lastError();

    ASSERT_FALSE(emittedIds.empty()) << "waitForIOChanged was never emitted";
    for (const auto &id : emittedIds)
        EXPECT_EQ(kIoId, id);
}
