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
#ifndef SCRIPTBINDINGS_H
#define SCRIPTBINDINGS_H

#include <Utils.h>
#include <lua.hpp>
#include <Lunar.h>
#include <IOBase.h>

#ifndef CALAOS_INSTALLER
#include <Output.h>
#include <Input.h>
#include <Ecore.h>
#include <Calaos.h>
#endif

namespace Calaos
{

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

                public:
                        Lua_Calaos();
                        Lua_Calaos(lua_State *L);
                        ~Lua_Calaos();

                        /* Output set/get */
                        int getOutputValue(lua_State *L);
                        int setOutputValue(lua_State *L);

                        /* Input get */
                        int getInputValue(lua_State *L);

                        /* Urel request */
                        int requestUrl(lua_State *L);
        };
}

#endif
