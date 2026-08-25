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
#include "ScriptBindings.h"
#include "ScriptManager.h"
#include "ScriptWire.h"
#include "UrlDownloader.h"
#include "libuvw.h"

using namespace Calaos;

//Debug
void Calaos::Lua_stackDump(lua_State *L)
{
    int i;
    int top = lua_gettop(L);
    for (i = 1; i <= top; i++)
    {  /* repeat for each level */
        int t = lua_type(L, i);
        switch (t)
        {
        case LUA_TSTRING:  /* strings */
            printf("`%s'", lua_tostring(L, i));
            break;

        case LUA_TBOOLEAN:  /* booleans */
            printf(lua_toboolean(L, i) ? "true" : "false");
            break;

        case LUA_TNUMBER:  /* numbers */
            printf("%g", lua_tonumber(L, i));
            break;

        default:  /* other values */
            printf("%s", lua_typename(L, t));
            break;
        }
        printf("  ");  /* put a separator */
    }
    printf("\n");  /* end the listing */
}

/* This print version will replace the one in base lib. It prints out everything
 * through the shared logging facility instead of stdout
 */
int Calaos::Lua_print(lua_State *L)
{
    int n = lua_gettop(L);
    string msg;

    for (int i = 1;i <= n;i++)
    {
        if (i > 1)
            msg += " ";
        if (lua_isstring(L, i))
            msg += lua_tostring(L, i);
        else if (lua_isnil(L, i))
            msg += "nil";
        else if (lua_isboolean(L, i))
            msg += lua_toboolean(L, i)? "true" : "false";
        else
            msg += Utils::to_string(luaL_typename(L, i)) + Utils::to_string(":") + Utils::to_string(lua_topointer(L, i));
    }

#ifdef CALAOS_INSTALLER
    LuaPrinter::Instance().Print(QString::fromUtf8(msg.c_str()));
#else
    cInfoDom("script.lua") << "LuaPrint: "<< msg;
#endif

    return 0;
}

void Calaos::Lua_DebugHook(lua_State *L, lua_Debug *ar)
{
    double elapsed;

    if (ScriptManager::watchdogExpired(elapsed))
    {
        string err = "Aborting script, takes too much time to execute (";
        err += Utils::to_string(elapsed) + " sec.)";

        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    ScriptManager::Instance().LuaDebugHook(L, ar);
}

Lunar<Lua_Calaos>::RegType Lua_Calaos::methods[] =
{
    { "getOutputValue", &Lua_Calaos::getIOValue },
    { "setOutputValue", &Lua_Calaos::setIOValue },
    { "getInputValue", &Lua_Calaos::getIOValue },
    { "getIOValue", &Lua_Calaos::getIOValue },
    { "setIOValue", &Lua_Calaos::setIOValue },
    { "getIOParam", &Lua_Calaos::getIOParam },
    { "setIOParam", &Lua_Calaos::setIOParam },
    { "waitForIO", &Lua_Calaos::waitForIO },
    { "requestUrl", &Lua_Calaos::requestUrl },
    { "sendPushNotif", &Lua_Calaos::sendPushNotif },
    { "getEnv", &Lua_Calaos::getEnv },
    { 0, 0 }
};

const char Lua_Calaos::className[] = "Calaos";

Lua_Calaos::Lua_Calaos()
{
}

Lua_Calaos::Lua_Calaos(lua_State *L)
{
    string err = "Calaos(): Don't create a new object, juste use the existing one \"calaos:...\"";
    lua_pushstring(L, err.c_str());
    lua_error(L);
}

int Lua_Calaos::getIOValue(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            string err = "getIOValue(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            auto io = ioMap[o];
            if (io.params["var_type"] == "float")
                lua_pushnumber(L, io.get_value_double());
            else if (io.params["var_type"] == "bool")
                lua_pushboolean(L, io.get_value_bool());
            else
                lua_pushstring(L, io.get_value_string().c_str());
        }
    }
    else
    {
        string err = "getIOValue(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 1;
}

int Lua_Calaos::setIOValue(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 2 && lua_isstring(L, 1))
    {
        string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            string err = "setIOValue(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            auto io = ioMap[o];
            if (lua_isnumber(L, 2))
                io.set_value((double)lua_tonumber(L, 2));
            else if (lua_isboolean(L, 2))
                io.set_value((bool)lua_toboolean(L, 2));
            else if (lua_isstring(L, 2))
                io.set_value(Utils::to_string(lua_tostring(L, 2)));
            else
            {
                string err = "setIOValue(): invalid IO id";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }
        }
    }
    else
    {
        string err = "setIOValue(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 0;
}

int Lua_Calaos::getIOParam(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 2 && lua_isstring(L, 1) && lua_isstring(L, 2))
    {
        string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            string err = "getIOParam(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            string key = Utils::to_string(lua_tostring(L, 2));
            auto io = ioMap[o];

            if (Utils::is_of_type<double>(io.params[key]))
            {
                double v;
                Utils::from_string(io.params[key], v);
                lua_pushnumber(L, v);
            }
            else if (io.params[key] == "true" ||
                     io.params[key] == "false")
                lua_pushboolean(L, io.params[key] == "true");
            else
                lua_pushstring(L, io.params[key].c_str());
        }
    }
    else
    {
        string err = "getIOParam(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 1;
}

int Lua_Calaos::setIOParam(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 3 && lua_isstring(L, 1) && lua_isstring(L, 2))
    {
        string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            string err = "setIOParam(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            string key = Utils::to_string(lua_tostring(L, 2));
            string value;

            auto io = ioMap[o];

            if (lua_isnumber(L, 3))
                value = Utils::to_string(lua_tonumber(L, 3));
            else if (lua_isboolean(L, 3))
                value = lua_toboolean(L, 3)?"true":"false";
            else if (lua_isstring(L, 3))
                value = Utils::to_string(lua_tostring(L, 3));
            else
            {
                string err = "setIOParam(): wrong value";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }

            io.set_param(key, value);
        }
    }
    else
    {
        string err = "setIOParam(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    /* T3.27: 0, not 1. This used to declare one result and push nothing, and
     * Lunar::thunk leaves the call arguments on the stack, so lua handed the
     * script back the top of it - the third argument, `value`. Every
     * `if calaos:setIOParam(...) then` therefore read true.
     *
     * 0 is the convention of this method table: the three getters
     * (getIOValue/getIOParam/getEnv) return 1 and push their value, the
     * setters and actions (setIOValue, requestUrl, sendPushNotif, print)
     * return 0. setIOValue() right above is this function's own sibling.
     *
     * NOT a pushed boolean, and the reason is measurable rather than stylistic:
     * LuaIOBase::set_param() is void and fire-and-forget over the ExternProc
     * socket, and every failure path here longjmps through lua_error(). There
     * is no success/failure information to report, so `lua_pushboolean(L, true)`
     * would be a hardcoded constant - it would keep the guard above reading
     * true forever, which is the defect under a better name.
     */
    return 0;
}

int Lua_Calaos::waitForIO(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        string id = lua_tostring(L, 1);
        if (ioMap.find(id) == ioMap.end())
        {
            string err = "waitForIO(): invalid IO id: " + id;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            //loop until io has reveived a change event
            //it doesnt actually loop, but the process is doing a blocking select
            {
                ScriptWatchdogPause pause;
                while (!waitForIOChanged.emit(id) && !abort) ;
            }

            if (abort)
            {
                string err = "waitForIO(): Abort script.";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }
        }
    }
    else
    {
        string err = "waitForIO(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    /* T3.27: 0, not 1. Same defect as setIOParam() above - the declared result
     * was never pushed, so the script got back the top of the stack, here the
     * only argument: the io id. A non-empty string is truthy, so
     * `if calaos:waitForIO(io) then` took the true branch unconditionally.
     *
     * Nothing to report either: this function returns normally only when
     * waitForIOChanged.emit() answered true AND abort is false. The abort case
     * raises. A pushed boolean could only ever be the constant true.
     */
    return 0;
}

int Lua_Calaos::requestUrl(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        string url = lua_tostring(L, 1);

        UrlDownloader *dl = new UrlDownloader(url, true);
        //T2.19: user-scripted URL, insecure by default (policy). No natural
        //per-item config exists here: the URL comes from the Lua code at
        //runtime, there is no device param to opt in to verification.
        dl->setInsecure();
        dl->httpGet();

        //we are in an extern process here, so run a loop to do the download
        //it will be stopped when the download is finished
        ScriptWatchdogPause pause;
        uvw::Loop::getDefault()->run();
    }
    else if (nb == 2 && lua_isstring(L, 1) && lua_isstring(L, 2))
    {
        string url = lua_tostring(L, 1);
        string post_data = lua_tostring(L, 2);

        UrlDownloader *dl = new UrlDownloader(url, true);
        //T2.19: user-scripted URL, insecure by default (policy), see above
        dl->setInsecure();
        dl->httpPost(string(), post_data);

        ScriptWatchdogPause pause;
        uvw::Loop::getDefault()->run();
    }
    else
    {
        string err = "requestUrl(): invalid argument. Requires a string (URL to call).";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 0;
}

int Lua_Calaos::getEnv(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        string key = lua_tostring(L, 1);
        string val;

        if (env.Exists(key))
        {
            val = env[key];
        }

        lua_pushstring(L, val.c_str());
    }
    else
    {
        string err = "getEnv(): invalid call, no arguments required.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 1;
}

int Lua_Calaos::sendPushNotif(lua_State *L)
{
    int argCount = lua_gettop(L);

    if (argCount == 1 && lua_isstring(L, 1))
    {
        //The message is whatever the user's Lua script passed. A Lua string
        //is a byte string, so it can carry any byte at all - which is why the
        //dump() behind this call carries an invalid-UTF-8 error handler.
        extClient->sendMessage(
                    ScriptWire::buildPushNotifMessage(string(lua_tostring(L, 1))));
    }
    else if (argCount == 2 && lua_isstring(L, 1) && lua_isstring(L, 2))
    {
        extClient->sendMessage(
                    ScriptWire::buildPushNotifMessage(
                        string(lua_tostring(L, 1)),
                        ScriptWire::PushAttachment{string(lua_tostring(L, 2))}));
    }
    else
    {
        string err = "sendPushNotif(): invalid arguments. Requires a string (message) and optional attachment (string).";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 0;
}

bool LuaIOBase::get_value_bool() const
{
    return params["state"] == "true";
}

double LuaIOBase::get_value_double() const
{
    double v = 0.0;
    if (Utils::is_of_type<double>(params["state"]))
        Utils::from_string(params["state"], v);
    return v;
}

string LuaIOBase::get_value_string() const
{
    return params["state"];
}

void LuaIOBase::set_value(bool val) const
{
    extClient->sendMessage(
                ScriptWire::buildSetStateMessage(ScriptWire::IoId{params["id"]},
                                                 val?"true":"false"));
}

void LuaIOBase::set_value(double val) const
{
    //Utils::to_string(double) is a bare ostringstream: six significant digits,
    //then scientific notation. Frozen behaviour, not corrected here.
    extClient->sendMessage(
                ScriptWire::buildSetStateMessage(ScriptWire::IoId{params["id"]},
                                                 Utils::to_string(val)));
}

void LuaIOBase::set_value(std::string val) const
{
    //val comes from the user's Lua script and can carry any byte.
    extClient->sendMessage(
                ScriptWire::buildSetStateMessage(ScriptWire::IoId{params["id"]}, val));
}

void LuaIOBase::set_param(const string &key, const string &val)
{
    //Three strings in a row: the named types make an exchange of any two of
    //them a compile error rather than a silent wire defect.
    extClient->sendMessage(
                ScriptWire::buildSetParamMessage(ScriptWire::IoId{params["id"]},
                                                 ScriptWire::ParamKey{key},
                                                 ScriptWire::ParamValue{val}));
}
