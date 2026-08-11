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
#include "ScriptManager.h"
#include <setjmp.h>
#include <chrono>

using namespace Calaos;

double ScriptManager::start_time = 0.0;
static jmp_buf panic_jmp;

static double watchdog_pause_start = 0.0;
static int watchdog_pause_count = 0;

namespace
{

/* Libraries opened at C level. They are only a source for the allow-list
 * below, none of them is handed to the script as it is, except for the ones
 * listed in sandbox_tables.
 */
const luaL_Reg sandbox_libs[] =
{
    { "", luaopen_base },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_TABLIBNAME, luaopen_table },
    { LUA_MATHLIBNAME, luaopen_math },
    { LUA_OSLIBNAME, luaopen_os },
    { nullptr, nullptr }
};

/* Base library functions a rule script is allowed to see. Everything else the
 * base library installs stays out: load/loadstring/loadfile/dofile load code
 * (including bytecode) from anywhere, getfenv/setfenv/rawget/rawset/rawequal/
 * setmetatable/getmetatable/newproxy escape the environment the sandbox is
 * built on, and collectgarbage/gcinfo/coroutine have no use in a rule.
 */
const char *const sandbox_base[] =
{
    "assert",
    "error",
    "ipairs",
    "next",
    "pairs",
    "pcall",
    "select",
    "tonumber",
    "tostring",
    "type",
    "unpack",
    "xpcall",
    nullptr
};

/* Handed over whole, they hold nothing that reaches outside of the interpreter */
const char *const sandbox_tables[] =
{
    LUA_STRLIBNAME,
    LUA_TABLIBNAME,
    LUA_MATHLIBNAME,
    nullptr
};

/* os is rebuilt from scratch with the time related functions only. execute,
 * remove, rename, getenv, exit, tmpname and setlocale are left out.
 */
const char *const sandbox_os[] =
{
    "clock",
    "date",
    "difftime",
    "time",
    nullptr
};

/* Builds the environment a rule script runs in from an explicit allow-list,
 * and installs it as the global table. luaL_openlibs() is never called: it
 * would also open io, debug, package (and with it require() and the preloaded
 * luajit ffi library), none of which can be reached from here.
 */
void buildSandboxEnv(lua_State *L)
{
    for (const luaL_Reg *lib = sandbox_libs;lib->func;lib++)
    {
        lua_pushcfunction(L, lib->func);
        lua_pushstring(L, lib->name);
        lua_call(L, 1, 0);
    }

    lua_newtable(L);
    int env = lua_gettop(L);

    for (const char *const *name = sandbox_base;*name;name++)
    {
        lua_getfield(L, LUA_GLOBALSINDEX, *name);
        lua_setfield(L, env, *name);
    }

    for (const char *const *name = sandbox_tables;*name;name++)
    {
        lua_getfield(L, LUA_GLOBALSINDEX, *name);
        lua_setfield(L, env, *name);
    }

    lua_newtable(L);
    for (const char *const *name = sandbox_os;*name;name++)
    {
        lua_getfield(L, LUA_GLOBALSINDEX, LUA_OSLIBNAME);
        lua_getfield(L, -1, *name);
        lua_remove(L, -2);
        lua_setfield(L, -2, *name);
    }
    lua_setfield(L, env, LUA_OSLIBNAME);

    /* print goes through the shared logging facility, not stdout */
    lua_pushcfunction(L, Lua_print);
    lua_setfield(L, env, "print");

    lua_pushvalue(L, env);
    lua_setfield(L, env, "_G");

    lua_replace(L, LUA_GLOBALSINDEX);
}

}

ScriptManager::ScriptManager()
{
    cDebugDom("script.lua");
}

double ScriptManager::monotonicTime()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

void ScriptManager::watchdogStart()
{
    watchdog_pause_count = 0;
    watchdog_pause_start = 0.0;
    start_time = monotonicTime();
}

void ScriptManager::watchdogPause()
{
    if (watchdog_pause_count++ == 0)
        watchdog_pause_start = monotonicTime();
}

void ScriptManager::watchdogResume()
{
    if (watchdog_pause_count <= 0)
        return;

    if (--watchdog_pause_count == 0)
        start_time += monotonicTime() - watchdog_pause_start;
}

bool ScriptManager::watchdogExpired(double &elapsed)
{
    if (watchdog_pause_count > 0)
    {
        elapsed = 0.0;
        return false;
    }

    elapsed = monotonicTime() - start_time;

    return elapsed > SCRIPT_MAX_EXEC_TIME;
}

bool ScriptManager::ExecuteScript(const string &script)
{
    bool ret = true;
    errorScript = true;

    lua_State *L = lua_open();

    buildSandboxEnv(L);

    Lunar<Lua_Calaos>::Register(L);
    Lunar<Lua_Calaos>::push(L, &luaCalaos);
    lua_setglobal(L, "calaos");

    //Call setlocale to change for C locale (and avoid problems with double value<>string conversion)
    setlocale(LC_ALL, "C");

    //Set a hook to kill script in case of a wrong use (infinite loop, ...)
    lua_sethook(L, Lua_DebugHook, LUA_MASKLINE | LUA_MASKCOUNT, 1);

    watchdogStart();

    int err = luaL_loadbuffer(L, script.c_str(), script.length(), "CalaosScript");
    if (err)
    {
        ret = false;
        if (err == LUA_ERRSYNTAX)
        {
            string msg = lua_tostring(L, -1);
            cErrorDom("script.lua") << "Syntax Error: " << msg;
            errorMsg = "Syntax Error:\n" + msg;
        }
        else if (err == LUA_ERRMEM)
        {
            string msg = lua_tostring(L, -1);
            cErrorDom("script.lua") << "LUA memory allocation error: " << msg;
            errorMsg = "Fatal Error:\nLUA memory allocation error:\n" + msg;
        }
    }
    else
    {
        if (setjmp(panic_jmp) == 1)
        {
            ret = false;
            cErrorDom("script.lua") << "Script panic !"                                       ;
            errorMsg = "Fatal Error:\nScript panic !";
        }

        if ((err = lua_pcall(L, 0, 1, 0)))
        {
            ret = false;

            string errcode;
            if (err == LUA_ERRRUN) errcode = "Runtime error";
            else if (err == LUA_ERRSYNTAX) errcode = "Syntax error";
            else if (err == LUA_ERRMEM) errcode = "Memory allocation error";
            else if (err == LUA_ERRERR) errcode = "Error";
            else errcode = "Unknown error";

            string msg = lua_tostring(L, -1);
            cErrorDom("script.lua") << errcode << " : " << msg;
            errorMsg = "Error " + errcode + " :\n\t" + msg;
        }
        else
        {
            if (!lua_isboolean(L, -1))
            {
                ret = false;
                cErrorDom("script.lua") << "Script must return either \"true\" or \"false\"";
                errorMsg = "Error:\nScript must return either \"true\" or \"false\"";
            }
            else
            {
                errorScript = false;
                ret = lua_toboolean(L, -1);
            }
        }
    }

    lua_close(L);

    return ret;
}

void ScriptManager::LuaDebugHook(lua_State *L, lua_Debug *ar)
{
    if (abort)
    {
        string err = "waitForIO(): Abort script.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    debugHook.emit();
}
