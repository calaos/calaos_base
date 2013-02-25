/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <ScriptBindings.h>
#include <ListeRoom.h>
#include <ScriptManager.h>

#ifdef CALAOS_INSTALLER
#include <QTime>
#include <DialogScriptEditor.h>
#endif

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
                        msg += to_string(luaL_typename(L, i)) + to_string(":") + to_string(lua_topointer(L, i));
        }

        #ifdef CALAOS_INSTALLER
        LuaPrinter::Instance().Print(QString::fromUtf8(msg.c_str()));
        #else
        Utils::logger("script.lua") << Priority::INFO << "LuaPrint: "<< msg << log4cpp::eol;
        #endif

        return 0;
}

void Calaos::Lua_DebugHook(lua_State *L, lua_Debug *ar)
{
        double time;

        #ifndef CALAOS_INSTALLER
        time = ecore_time_get();
        #else
        QTime t = QTime::currentTime();
        time = (double)t.second() + (((double) t.msec()) / 1000);
        #endif

        if (time - ScriptManager::start_time > SCRIPT_MAX_EXEC_TIME)
        {
                string err = "Aborting script, takes too much time to execute (";
                err += to_string(time - ScriptManager::start_time) + " sec.)";

                lua_pushstring(L, err.c_str());
                lua_error(L);
        }
}

Lunar<Lua_Calaos>::RegType Lua_Calaos::methods[] =
{
        { "getOutputValue", &Lua_Calaos::getOutputValue },
        { "setOutputValue", &Lua_Calaos::setOutputValue },
        { "getInputValue", &Lua_Calaos::getInputValue },
        { "requestUrl", &Lua_Calaos::requestUrl },
        { 0, 0 }
};

const char Lua_Calaos::className[] = "Calaos";

Lua_Calaos::Lua_Calaos()
{
        #ifndef CALAOS_INSTALLER
        Utils::logger("script.lua") << Priority::DEBUG << "Lua_Calaos::Lua_Calaos(): Ok " << log4cpp::eol;
        #endif
}

Lua_Calaos::Lua_Calaos(lua_State *L)
{
        string err = "Calaos(): Don't create a new object, juste use the existing one \"calaos:...\"";
        lua_pushstring(L, err.c_str());
        lua_error(L);
}

Lua_Calaos::~Lua_Calaos()
{
        #ifndef CALAOS_INSTALLER
        Utils::logger("script.lua") << Priority::DEBUG << "Lua_Calaos::~Lua_Calaos(): Ok " << log4cpp::eol;
        #endif
}

int Lua_Calaos::getInputValue(lua_State *L)
{
        int nb = lua_gettop(L);

        if (nb == 1 && lua_isstring(L, 1))
        {
                string o = lua_tostring(L, 1);
                Input *input = ListeRoom::Instance().get_input(o);

                if (!input)
                {
                        string err = "getInputValue(): invalid input";
                        lua_pushstring(L, err.c_str());
                        lua_error(L);
                }
                else
                {
                        switch (input->get_type())
                        {
                                case TINT: lua_pushnumber(L, input->get_value_double()); break;
                                case TBOOL: lua_pushboolean(L, input->get_value_bool()); break;
                                case TSTRING: lua_pushstring(L, input->get_value_string().c_str()); break;
                                default:
                                {
                                        string err = "getInputValue(): invalid input";
                                        lua_pushstring(L, err.c_str());
                                        lua_error(L);
                                }
                        }
                }
        }
        else
        {
                string err = "getInputValue(): invalid argument. Requires an Input ID.";
                lua_pushstring(L, err.c_str());
                lua_error(L);
        }

        return 1;
}

int Lua_Calaos::getOutputValue(lua_State *L)
{
        int nb = lua_gettop(L);

        if (nb == 1 && lua_isstring(L, 1))
        {
                string o = lua_tostring(L, 1);
                Output *output = ListeRoom::Instance().get_output(o);

                if (!output)
                {
                        string err = "getOutputValue(): invalid output";
                        lua_pushstring(L, err.c_str());
                        lua_error(L);
                }
                else
                {
                        switch (output->get_type())
                        {
                                case TINT: lua_pushnumber(L, output->get_value_double()); break;
                                case TBOOL: lua_pushboolean(L, output->get_value_bool()); break;
                                case TSTRING: lua_pushstring(L, output->get_value_string().c_str()); break;
                                default:
                                {
                                        string err = "getOutputValue(): invalid input";
                                        lua_pushstring(L, err.c_str());
                                        lua_error(L);
                                }
                        }
                }
        }
        else
        {
                string err = "getOutputValue(): invalid argument. Requires an Output ID.";
                lua_pushstring(L, err.c_str());
                lua_error(L);
        }

        return 1;
}

int Lua_Calaos::setOutputValue(lua_State *L)
{
        int nb = lua_gettop(L);

        if (nb == 2 && lua_isstring(L, 1))
        {
                string o = lua_tostring(L, 1);
                Output *output = ListeRoom::Instance().get_output(o);

                if (!output)
                {
                        string err = "getOutputValue(): invalid output";
                        lua_pushstring(L, err.c_str());
                        lua_error(L);
                }
                else
                {
                        if (lua_isnumber(L, 2))
                                output->set_value(lua_tonumber(L, 2));
                        else if (lua_isboolean(L, 2))
                                output->set_value((bool)lua_toboolean(L, 2));
                        else if (lua_isstring(L, 2))
                                output->set_value(to_string(lua_tostring(L, 2)));
                        else
                        {
                                string err = "setOutputValue(): wrong value";
                                lua_pushstring(L, err.c_str());
                                lua_error(L);
                        }
                }
        }
        else
        {
                string err = "getOutputValue(): invalid argument. Requires an Output ID.";
                lua_pushstring(L, err.c_str());
                lua_error(L);
        }

        return 0;
}

int Lua_Calaos::requestUrl(lua_State *L)
{
        int nb = lua_gettop(L);

        if (nb == 1 && lua_isstring(L, 1))
        {
                string url = lua_tostring(L, 1);

                #ifndef CALAOS_INSTALLER
                CallUrl(url);
                #endif
        }
        else if (nb == 2 && lua_isstring(L, 1) && lua_isstring(L, 2))
        {
                string url = lua_tostring(L, 1);
                string post_data = lua_tostring(L, 2);

                #ifndef CALAOS_INSTALLER
                CallUrl(url, post_data);
                #endif
        }
        else
        {
                string err = "requestUrl(): invalid argument. Requires a string (URL to call).";
                lua_pushstring(L, err.c_str());
                lua_error(L);
        }

        return 0;
}

