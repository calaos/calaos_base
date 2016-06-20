/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include "FileDownloader.h"

namespace Calaos {

//Debug
void Lua_stackDump(lua_State *L)
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
int Lua_print(lua_State *L)
{
    int n = lua_gettop(L);
    std::string msg;

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

void Lua_DebugHook(lua_State *L, lua_Debug *ar)
{
    ScriptManager::Instance().LuaDebugHook(L, ar);
//    double time;
//
//    time = ecore_time_get();
//
//    if (time - ScriptManager::start_time > SCRIPT_MAX_EXEC_TIME)
//    {
//        string err = "Aborting script, takes too much time to execute (";
//        err += Utils::to_string(time - ScriptManager::start_time) + " sec.)";
//
//        lua_pushstring(L, err.c_str());
//        lua_error(L);
//    }
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
    { 0, 0 }
};

const char Lua_Calaos::className[] = "Calaos";

Lua_Calaos::Lua_Calaos()
{
}

Lua_Calaos::Lua_Calaos(lua_State *L)
{
    std::string err = "Calaos(): Don't create a new object, juste use the existing one \"calaos:...\"";
    lua_pushstring(L, err.c_str());
    lua_error(L);
}

int Lua_Calaos::getIOValue(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        std::string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            std::string err = "getIOValue(): invalid IO id: " + o;
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
        std::string err = "getIOValue(): invalid argument. Requires an IO id.";
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
        std::string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            std::string err = "setIOValue(): invalid IO id: " + o;
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
                std::string err = "setIOValue(): invalid IO id";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }
        }
    }
    else
    {
        std::string err = "setIOValue(): invalid argument. Requires an IO id.";
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
        std::string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            std::string err = "getIOParam(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            std::string key = Utils::to_string(lua_tostring(L, 2));
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
        std::string err = "getIOParam(): invalid argument. Requires an IO id.";
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
        std::string o = lua_tostring(L, 1);
        if (ioMap.find(o) == ioMap.end())
        {
            std::string err = "setIOParam(): invalid IO id: " + o;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            std::string key = Utils::to_string(lua_tostring(L, 2));
            std::string value;

            auto io = ioMap[o];

            if (lua_isnumber(L, 3))
                value = Utils::to_string(lua_tonumber(L, 3));
            else if (lua_isboolean(L, 3))
                value = lua_toboolean(L, 3)?"true":"false";
            else if (lua_isstring(L, 3))
                value = Utils::to_string(lua_tostring(L, 3));
            else
            {
                std::string err = "setIOParam(): wrong value";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }

            io.set_param(key, value);
        }
    }
    else
    {
        std::string err = "setIOParam(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 1;
}

int Lua_Calaos::waitForIO(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        std::string id = lua_tostring(L, 1);
        if (ioMap.find(id) == ioMap.end())
        {
            std::string err = "waitForIO(): invalid IO id: " + id;
            lua_pushstring(L, err.c_str());
            lua_error(L);
        }
        else
        {
            //loop until io has reveived a change event
            //it doesnt actually loop, but the process is doing a blocking select
            while (!waitForIOChanged.emit(id) && !abort) ;

            if (abort)
            {
                std::string err = "waitForIO(): Abort script.";
                lua_pushstring(L, err.c_str());
                lua_error(L);
            }
        }
    }
    else
    {
        std::string err = "waitForIO(): invalid argument. Requires an IO id.";
        lua_pushstring(L, err.c_str());
        lua_error(L);
    }

    return 1;
}

int Lua_Calaos::requestUrl(lua_State *L)
{
    int nb = lua_gettop(L);

    if (nb == 1 && lua_isstring(L, 1))
    {
        std::string url = lua_tostring(L, 1);

        std::string code = "local http = require(\"socket.http\")\n"    \
                      "http.request(\"" + url + "\")";

        luaL_loadstring(L, code.c_str());
        lua_call(L, 0, 0);
    }
    else if (nb == 2 && lua_isstring(L, 1) && lua_isstring(L, 2))
    {
        std::string url = lua_tostring(L, 1);
        std::string post_data = lua_tostring(L, 2);

        std::string code = "local http = require(\"socket.http\")\n"    \
                      "http.request(\"" + url + "\", \"" + Utils::escape_quotes(post_data) + "\")";

        luaL_loadstring(L, code.c_str());
        lua_call(L, 0, 0);
    }
    else
    {
        std::string err = "requestUrl(): invalid argument. Requires a string (URL to call).";
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

std::string LuaIOBase::get_value_string() const
{
    return params["state"];
}

void LuaIOBase::set_value(bool val) const
{
    Params p = {{ "id", params["id"] },
                { "value", val?"true":"false" }};

    sendJson("set_state", p);
}

void LuaIOBase::set_value(double val) const
{
    Params p = {{ "id", params["id"] },
                { "value", Utils::to_string(val) }};

    sendJson("set_state", p);
}

void LuaIOBase::set_value(std::string val) const
{
    Params p = {{ "id", params["id"] },
                { "value", val }};

    sendJson("set_state", p);
}

void LuaIOBase::set_param(const std::string &key, const std::string &val)
{
    Params p = {{ "id", params["id"] },
                { "param", key },
                { "value", val }};

    sendJson("set_param", p);
}

void LuaIOBase::sendJson(const std::string &msg_type, const Params &param) const
{
    json_t *jroot = json_object();
    json_object_set_new(jroot, "msg", json_string(msg_type.c_str()));
    json_object_set_new(jroot, "data", param.toJson());
    extClient->sendMessage(jansson_to_string(jroot));
}

}
