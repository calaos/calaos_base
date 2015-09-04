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
#ifndef SCRIPTMANAGER_H
#define SCRIPTMANAGER_H

#include "Utils.h"
#include "Ecore.h"
#include "ScriptBindings.h"
#include "ExternProc.h"

namespace Calaos
{

//The maximum number of second a script can take to do his duty
//After that, it will be stopped.
#define SCRIPT_MAX_EXEC_TIME    2.0


static const luaL_Reg lua_libs[] =
{
    { "", luaopen_base },
    // { LUA_LOADLIBNAME, luaopen_package },
    { LUA_TABLIBNAME, luaopen_table },
    // { LUA_IOLIBNAME, luaopen_io },
    { LUA_OSLIBNAME, luaopen_os },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_MATHLIBNAME, luaopen_math },
    // { LUA_DBLIBNAME, luaopen_debug },

    { NULL, NULL }
};

class ScriptManager
{
private:
    ScriptManager();

    bool errorScript;
    string errorMsg;

public:
    static ScriptManager &Instance()
    {
        static ScriptManager sm;
        return sm;
    }

    ~ScriptManager();

    /** Execute script and return true or false depending on
                  * the return value of the script
                  */
    bool ExecuteScript(const string &script);

    ExternProcServer *ExecuteScriptDetached(const string &script, std::function<void(bool ret)> cb);

    /** Retrieve the last error message
                  */
    string getErrorMsg() { return errorMsg; }

    bool hasError() { return errorScript; }

    static double start_time;
};

}
#endif
