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
#ifndef SCRIPTMANAGER_H
#define SCRIPTMANAGER_H

#include "Utils.h"
#include "ScriptBindings.h"

/* Maximum time, in seconds, a script may spend inside the lua interpreter
 * before it gets aborted. Time spent blocked in a binding that waits on the
 * outside world (waitForIO(), requestUrl()) is not counted.
 */
#define SCRIPT_MAX_EXEC_TIME 5.0

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

    /** Monotonic timestamp, in seconds, at which the running script started */
    static double start_time;

    /* Utils::getMainLoopTime() is not usable for the watchdog: it returns the
     * libuv cached loop time, which is only refreshed when the loop iterates.
     * A script runs to completion inside a single iteration, so that value
     * stays frozen for its whole execution.
     */
    static double monotonicTime();

    /** Arms the watchdog for a new script */
    static void watchdogStart();

    /* Bindings that block on the outside world (waitForIO(), requestUrl()) do
     * not count against the execution limit.
     */
    static void watchdogPause();
    static void watchdogResume();

    /** true when the running script went over SCRIPT_MAX_EXEC_TIME */
    static bool watchdogExpired(double &elapsed);

    void LuaDebugHook(lua_State *L, lua_Debug *ar);

    sigc::signal<void> debugHook;

    Lua_Calaos luaCalaos;

    void abortScript() { abort = true; luaCalaos.abort = true; }
};

/* Suspends the execution watchdog for the lifetime of the object. Never keep
 * one alive across a lua_error() call, the longjmp would skip the destructor.
 */
class ScriptWatchdogPause
{
public:
    ScriptWatchdogPause() { ScriptManager::watchdogPause(); }
    ~ScriptWatchdogPause() { ScriptManager::watchdogResume(); }

    ScriptWatchdogPause(const ScriptWatchdogPause &) = delete;
    ScriptWatchdogPause &operator=(const ScriptWatchdogPause &) = delete;
};

}
#endif
