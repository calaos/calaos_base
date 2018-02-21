/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "ScriptBindings.h"

namespace Calaos
{

class ScriptManager
{
private:
    ScriptManager();

    bool errorScript;
    string errorMsg;
    bool abort = false;

public:
    static ScriptManager &Instance()
    {
        static ScriptManager sm;
        return sm;
    }

    /* Execute script and return true or false depending on
     * the return value of the script
     */
    bool ExecuteScript(const string &script);

    /** Retrieve the last error message */
    string getErrorMsg() { return errorMsg; }

    bool hasError() { return errorScript; }

    static double start_time;

    void LuaDebugHook(lua_State *L, lua_Debug *ar);

    sigc::signal<void> debugHook;

    Lua_Calaos luaCalaos;

    void abortScript() { abort = true; luaCalaos.abort = true; }
};

}
#endif
