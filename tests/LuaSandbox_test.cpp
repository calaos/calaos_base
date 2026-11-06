// T1.15: the lua environment handed to user rule scripts is built from an
// explicit allow-list (the calaos:* bindings plus a safe stdlib subset), and a
// wall clock watchdog aborts a script that never hands the interpreter back.
//
// Everything goes through ScriptManager::ExecuteScript(): it is the single
// place in the tree where a lua_State is created. calaos_server never runs a
// script itself, it spawns calaos_script (ScriptExec::ExecuteScriptDetached)
// and that process calls ExecuteScript() from ScriptExtern_main.cpp. Testing
// the manager directly therefore covers the only execution path there is.

#include "ScriptManager.h"
#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>

using namespace Calaos;

namespace
{

bool runScript(const std::string &script)
{
    return ScriptManager::Instance().ExecuteScript(script);
}

bool scriptFailed()
{
    return ScriptManager::Instance().hasError();
}

std::string lastError()
{
    return ScriptManager::Instance().getErrorMsg();
}

//Evaluates "<expr> == nil" inside a script. Returns true when the name is not
//reachable at all from the script environment.
bool globalIsNil(const std::string &expr)
{
    bool ret = runScript("return " + expr + " == nil");
    return ret && !scriptFailed();
}

double timeScript(const std::string &script)
{
    auto t0 = std::chrono::steady_clock::now();
    runScript(script);
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count();
}

}

//Everything that gives a script a way out of the interpreter: process
//execution, filesystem, native code loading, bytecode loading, introspection.
TEST(LuaSandbox, DangerousGlobalsAreAbsent)
{
    const std::vector<std::string> denied = {
        "io",
        "os.execute",
        "os.remove",
        "os.rename",
        "os.getenv",
        "os.exit",
        "os.tmpname",
        "os.setlocale",
        "package",
        "require",
        "module",
        "load",
        "loadstring",
        "loadfile",
        "dofile",
        "debug",
        "ffi",
        "jit",
        "getfenv",
        "setfenv",
        "setmetatable",
        "getmetatable",
        "rawget",
        "rawset",
        "rawequal",
        "collectgarbage",
        "newproxy",
        "gcinfo",
        "coroutine",
    };

    for (const auto &name : denied)
        EXPECT_TRUE(globalIsNil(name)) << name << " is reachable from a rule script";
}

//The ffi library is preloaded by luaL_openlibs() on luajit: with package and
//require gone there is no way left to instantiate it.
TEST(LuaSandbox, FfiCannotBeLoaded)
{
    EXPECT_TRUE(globalIsNil("package"));
    EXPECT_TRUE(globalIsNil("require"));

    //_G must not carry a back door to the unsandboxed globals either
    EXPECT_TRUE(runScript("return _G.io == nil and _G.os.execute == nil"));
    EXPECT_FALSE(scriptFailed());
}

//Calling a denied function is a plain lua runtime error: the script fails,
//nothing runs on the system, and the interpreter is still usable afterwards.
TEST(LuaSandbox, CallingOsExecuteFailsAndRunsNothing)
{
    std::string witness = "/tmp/calaos_lua_sandbox_witness";
    ::unlink(witness.c_str());

    EXPECT_FALSE(runScript("os.execute(\"touch " + witness + "\") return true"));
    EXPECT_TRUE(scriptFailed());
    EXPECT_NE(lastError().find("attempt to call"), std::string::npos) << lastError();

    struct stat st;
    EXPECT_NE(::stat(witness.c_str(), &st), 0) << "os.execute() actually ran a shell command";
    ::unlink(witness.c_str());

    //the engine keeps working after a denied call
    EXPECT_TRUE(runScript("return true"));
    EXPECT_FALSE(scriptFailed());
}

TEST(LuaSandbox, IoOpenFailsAndWritesNothing)
{
    std::string witness = "/tmp/calaos_lua_sandbox_witness_io";
    ::unlink(witness.c_str());

    EXPECT_FALSE(runScript("local f = io.open(\"" + witness + "\", \"w\") f:write(\"x\") return true"));
    EXPECT_TRUE(scriptFailed());

    struct stat st;
    EXPECT_NE(::stat(witness.c_str(), &st), 0) << "io.open() actually created a file";
    ::unlink(witness.c_str());
}

//The allow-list must still cover what a real rule script uses.
TEST(LuaSandbox, SafeStandardLibraryStillWorks)
{
    const std::string script =
        "local t = {}\n"
        "for i = 1, 5 do table.insert(t, string.format(\"%d\", i * 2)) end\n"
        "local joined = table.concat(t, \",\")\n"
        "if joined ~= \"2,4,6,8,10\" then return false end\n"
        "if string.upper(\"ab\") ~= \"AB\" then return false end\n"
        "if (\"ab\"):rep(2) ~= \"abab\" then return false end\n"
        "if math.floor(math.max(1.7, 1.2)) ~= 1 then return false end\n"
        "if tonumber(\"42\") + 0 ~= 42 then return false end\n"
        "if tostring(42) ~= \"42\" then return false end\n"
        "if type(t) ~= \"table\" then return false end\n"
        "local n = 0\n"
        "for _, _ in pairs(t) do n = n + 1 end\n"
        "for _, _ in ipairs(t) do n = n + 1 end\n"
        "if n ~= 10 then return false end\n"
        "if select(\"#\", 1, 2, 3) ~= 3 then return false end\n"
        "if next({}) ~= nil then return false end\n"
        "local ok = pcall(function() error(\"boom\") end)\n"
        "if ok then return false end\n"
        "assert(true)\n"
        "print(\"lua sandbox test\")\n"
        "return true\n";

    EXPECT_TRUE(runScript(script));
    EXPECT_FALSE(scriptFailed());
}

//os is rebuilt from scratch with the time related functions only.
TEST(LuaSandbox, TimeOnlyOsFunctionsAreKept)
{
    const std::string script =
        "if type(os) ~= \"table\" then return false end\n"
        "local t = os.time()\n"
        "if type(t) ~= \"number\" then return false end\n"
        "if type(os.clock()) ~= \"number\" then return false end\n"
        "if type(os.date(\"%Y\")) ~= \"string\" then return false end\n"
        "if os.difftime(t, t) ~= 0 then return false end\n"
        "return true\n";

    EXPECT_TRUE(runScript(script));
    EXPECT_FALSE(scriptFailed());
}

TEST(LuaSandbox, CalaosApiIsExposed)
{
    EXPECT_TRUE(runScript("return type(calaos) == \"userdata\""));
    EXPECT_FALSE(scriptFailed());

    //getEnv() on an unset key gives an empty string, and the other bindings are
    //at least reachable as functions through the Lunar method table
    const std::string script =
        "if calaos:getEnv(\"no_such_key\") ~= \"\" then return false end\n"
        "if type(calaos.getIOValue) ~= \"function\" then return false end\n"
        "if type(calaos.setIOValue) ~= \"function\" then return false end\n"
        "if type(calaos.requestUrl) ~= \"function\" then return false end\n"
        "return true\n";

    EXPECT_TRUE(runScript(script));
    EXPECT_FALSE(scriptFailed());
}

//Watchdog: an infinite loop never returns to the interpreter on its own, the
//debug hook has to break it. Also proves the hook still fires on luajit, where
//a compiled trace would run outside of it.
TEST(LuaSandbox, InfiniteLoopIsAbortedByTheWatchdog)
{
    double elapsed = timeScript("while true do end return true");

    EXPECT_TRUE(scriptFailed());
    EXPECT_NE(lastError().find("too much time"), std::string::npos) << lastError();
    EXPECT_GE(elapsed, SCRIPT_MAX_EXEC_TIME * 0.5);
    EXPECT_LE(elapsed, SCRIPT_MAX_EXEC_TIME + 5.0) << "watchdog fired after " << elapsed << " sec.";

    //the engine survives, and the next script gets a fresh watchdog window
    EXPECT_TRUE(runScript("local x = 0 for i = 1, 1000 do x = x + i end return x == 500500"));
    EXPECT_FALSE(scriptFailed());
}

//A hot numeric loop is what the jit compiler would turn into a trace.
TEST(LuaSandbox, HotNumericLoopIsAbortedByTheWatchdog)
{
    double elapsed = timeScript("local x = 0 for i = 1, 1e12 do x = x + i end return true");

    EXPECT_TRUE(scriptFailed());
    EXPECT_NE(lastError().find("too much time"), std::string::npos) << lastError();
    EXPECT_LE(elapsed, SCRIPT_MAX_EXEC_TIME + 5.0) << "watchdog fired after " << elapsed << " sec.";
}
