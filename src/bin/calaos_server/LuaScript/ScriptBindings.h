/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef SCRIPTBINDINGS_H
#define SCRIPTBINDINGS_H

#include "Utils.h"
#include "lua.hpp"
#include "Lunar.h"
#include <Ecore.h>
#include "ExternProc.h"

namespace Calaos
{

//strip down version of IOBase for LUA extern proc
class LuaIOBase
{
public:
    LuaIOBase(ExternProcClient *e):
        extClient(e)
    {}

    Params params;

    bool get_value_bool();
    double get_value_double();
    string get_value_string();

    void set_value(bool val);
    void set_value(double val);
    void set_value(std::string val);

private:
    ExternProcClient *extClient;

    void sendJson(const string &msg_type, const Params &param);
};

int Lua_print(lua_State *L);
void Lua_DebugHook(lua_State *L, lua_Debug *ar);

//This is for debugging purpose only
void Lua_stackDump(lua_State *L);

class Lua_Calaos
{
private:
    friend class Lunar<Lua_Calaos>;
    static const char className[];
    static Lunar<Lua_Calaos>::RegType methods[];

    unordered_map<string, LuaIOBase *> ioMap;

public:
    Lua_Calaos();
    Lua_Calaos(lua_State *L);
    ~Lua_Calaos();

    /* IO set/get */
    int getIOValue(lua_State *L);
    int setIOValue(lua_State *L);

    /* Urel request */
    int requestUrl(lua_State *L);
};
}

#endif
