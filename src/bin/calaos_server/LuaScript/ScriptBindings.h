/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
    LuaIOBase():
        extClient(nullptr)
    {}

    Params params;

    bool get_value_bool() const;
    double get_value_double() const ;
    string get_value_string() const;

    void set_value(bool val) const;
    void set_value(double val) const;
    void set_value(std::string val) const;

    void set_param(const string &key, const string &val);

private:
    ExternProcClient *extClient;

    void sendJson(const string &msg_type, const Params &param) const;
};

int Lua_print(lua_State *L);
void Lua_DebugHook(lua_State *L, lua_Debug *ar);

//This is for debugging purpose only
void Lua_stackDump(lua_State *L);

class Lua_Calaos: sigc::trackable
{
private:
    friend class Lunar<Lua_Calaos>;
    static const char className[];
    static Lunar<Lua_Calaos>::RegType methods[];

public:
    Lua_Calaos();
    Lua_Calaos(lua_State *L);

    unordered_map<string, LuaIOBase> ioMap;
    bool abort = false;

    sigc::signal<bool, const string &> waitForIOChanged;

    /* IO set/get */
    int getIOValue(lua_State *L);
    int setIOValue(lua_State *L);

    int getIOParam(lua_State *L);
    int setIOParam(lua_State *L);

    int waitForIO(lua_State *L);

    /* Url request */
    int requestUrl(lua_State *L);
};
}

#endif
