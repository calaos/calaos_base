/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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

using namespace Calaos;

double ScriptManager::start_time = 0.0;
static jmp_buf panic_jmp;

ScriptManager::ScriptManager()
{
    cDebugDom("script.lua");
}

bool ScriptManager::ExecuteScript(const string &script)
{
    bool ret = true;
    errorScript = true;

    lua_State *L = lua_open();

    luaL_openlibs(L);

    /* disable some calls from base lib */
    lua_pushnil(L);
    lua_setglobal(L, "print"); /* we provide another version */

    /* register new functions */
    lua_register(L, "print", Lua_print);

    Lunar<Lua_Calaos>::Register(L);
    Lunar<Lua_Calaos>::push(L, &luaCalaos);
    lua_setglobal(L, "calaos");

    //Call setlocale to change for C locale (and avoid problems with double value<>string conversion)
    setlocale(LC_ALL, "C");

    //Set a hook to kill script in case of a wrong use (infinite loop, ...)
    lua_sethook(L, Lua_DebugHook, LUA_MASKLINE | LUA_MASKCOUNT, 1);

    start_time = Utils::getMainLoopTime();

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
