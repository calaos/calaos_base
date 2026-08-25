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
#include "ScriptExec.h"
#include "Prefix.h"
#include "Timer.h"
#include "JsonApi.h"
#include "EventManager.h"
#include "ActionPush.h"
#include "ScriptWire.h"

/* E4.1m migrated this file whole. Both halves of the Lua wire now speak the
 * same library, and every shape of it lives in ScriptWire.h, written and
 * covered by E4.1j (tests/ScriptWire_test.cpp) for exactly this moment.
 *
 * ONE CONTRACT IS KEPT BY HAND rather than by a shortcut, and ScriptWire says
 * why at length: ScriptWire::decodeObject() flattens a value of ANY type into
 * a Params entry - a string as is, a boolean as the WORD "true"/"false", a
 * number through Utils::to_string(double), anything else the empty string WITH
 * THE KEY STILL THERE. Params::fromNJson() is NOT a substitute: it assigns the
 * value straight into a std::string and throws type_error.302 on everything
 * that is not a JSON string.
 *
 * ONE BEHAVIOUR CHANGES, and it is declared in RELEASE_NOTES.md: a script text
 * carrying a byte that is not valid UTF-8 used to have its WHOLE pair dropped
 * by the previous emitter, so calaos_script received an "execute" message with
 * no script at all and ran nothing. It now arrives, with U+FFFD in place of
 * the bad byte, so the script RUNS. ScriptWire.h predicted this ticket would
 * open that channel; it does.
 */

//json_object_get(jroot, "data") was total: it answered NULL on a NULL and on
//anything that is not an object, and json_object_foreach() then iterated zero
//times. find() on a const Json answers cend() in exactly the same cases, and
//it cannot CREATE the key the way operator[] would.
static void decodeDataObject(const Json &jroot, Params &params)
{
    const Json::const_iterator it = jroot.find("data");
    if (it != jroot.cend())
        ScriptWire::decodeObject(*it, params);
}

enum
{
    ProcessNone = 0,
    ProcessStarted = 1,
    ProcessFinished = 2,
    ProcessExited = 3,
};
static unordered_map<ExternProcServer *, int> processStatus;

/* Second line of defense behind the in script watchdog (SCRIPT_MAX_EXEC_TIME):
 * that one can only break a script that is running lua code, it cannot do
 * anything for a child wedged inside a binding or one that never answers at
 * all. The bound is deliberately far above SCRIPT_MAX_EXEC_TIME because
 * calaos:waitForIO() legitimately parks a script for as long as the IO it waits
 * for takes to change.
 */
#define SCRIPT_PROCESS_MAX_LIFETIME 3600.0

ExternProcServer *ScriptExec::ExecuteScriptDetached(const string &script, std::function<void(bool ret)> cb, Params env)
{
    ExternProcServer *process = new ExternProcServer("lua");
    cInfoDom("lua") << "Starting script. (" << process << ")";

    JsonApi *jsonApi = new JsonApi();
    sigc::connection *evcon = new sigc::connection;
    Timer **lifetime = new Timer *(nullptr);

    processStatus[process] = ProcessNone;

    process->messageReceived.connect([=](const string &msg)
    {
        if (processStatus.find(process) == processStatus.end() ||
            processStatus[process] != ProcessStarted)
            return;

        cDebug() << "Message received for process:" << process;

        Json jroot;
        if (!ScriptWire::parseMessage(msg, jroot))
        {
            //The parser detail the previous error text carried has no
            //equivalent in a non throwing parse, so it is gone; the raw
            //message next to it is what a reader actually needs. Same choice
            //as WagoWire, ReolinkWire and ScriptWire itself.
            cWarningDom("lua") << "Error parsing json from sub process. Raw message: " << msg;
            return;
        }

        string mtype = ScriptWire::stringGet(jroot, "msg");

        if (mtype == "finished")
        {
            cInfoDom("lua") << "LUA script finished.";
            process->terminate();
            //A WORD, not a JSON boolean: a real boolean would read back as
            //the default and every script would look like it failed.
            string ret = ScriptWire::stringGet(jroot, "return_val", "false");
            processStatus[process] = ProcessFinished;
            cb(ret == "true"); //process finished, call callback, process will be deleted later
        }
        else if (mtype == "set_state")
        {
            Params p;
            decodeDataObject(jroot, p);
            if (!jsonApi->decodeSetState(p))
                cWarningDom("lua") << "Failed to decode set_state from Lua Script!";
        }
        else if (mtype == "set_param")
        {
            Params p;
            decodeDataObject(jroot, p);
            if (!jsonApi->buildJsonSetParam(p))
                cWarningDom("lua") << "Failed to decode set_param from Lua Script!";
        }
        else if (mtype == "send_push_notif")
        {
            Params p;
            decodeDataObject(jroot, p);

            ActionPush *push = new ActionPush(p["message"], p["attachment"]);
            push->notifSent.connect([push]()
            {
                cDebugDom("lua") << "ActionPush finished, deleting...";
                delete push;
            });
            push->Execute();
        }
    });

    process->processExited.connect([=]()
    {
        cInfoDom("lua") << "LUA process terminated. (" << process << ")";
        evcon->disconnect();
        delete evcon;

        delete *lifetime;
        delete lifetime;

        if (processStatus[process] != ProcessFinished) //the callback was never called, force the call here
            cb(false);
        processStatus[process] = ProcessNone;

        Idler::singleIdler([=]()
        {
            delete jsonApi;
            delete process;
            processStatus.erase(process);
        });
    });

    //when process is connected, send the script to be executed
    process->processConnected.connect([=]()
    {
        processStatus[process] = ProcessStarted;

        *lifetime = new Timer(SCRIPT_PROCESS_MAX_LIFETIME, [=]()
        {
            if (processStatus.find(process) == processStatus.end() ||
                processStatus[process] != ProcessStarted)
                return;

            cErrorDom("lua") << "LUA script is still running after " << SCRIPT_PROCESS_MAX_LIFETIME
                             << " sec., killing process. (" << process << ")";
            process->terminate();
        });

        cDebug() << "Process connected. process:" << process;
        Params p = {{ "msg", "execute" },
                    { "script", script } };

        Json jroot = p.toNJson();

        //send the full calaos context here. (using JsonApi) to the process
        //after connect process to calaos events, and send him event so the process
        //can update its local cache of IO states.
        jroot["context"] = jsonApi->buildFlatIOList();

        //Also append the env to the json. Actually the env can contain which io has triggered the script
        jroot["env"] = env.toNJson();

        process->sendMessage(ScriptWire::dumpJson(jroot));

        //After initial context, send all events to the external process
        *evcon = std::move(EventManager::Instance().newEvent.connect([=](const CalaosEvent &ev)
        {
            //only send IO events
            if (ev.getType() == CalaosEvent::EventIOAdded ||
                ev.getType() == CalaosEvent::EventIODeleted ||
                ev.getType() == CalaosEvent::EventIOChanged ||
                ev.getType() == CalaosEvent::EventIOPropertyDelete ||
                ev.getType() == CalaosEvent::EventIOStatusChanged)
            {
                /* E4.1m: the transitional adapter E4.1l wrote for this ONE
                 * call site is gone, and so is the round trip it did. What
                 * changes on this wire, on top of the key order E4.1l already
                 * declared: the escape of a non ASCII character loses its
                 * uppercase hexadecimal, because that round trip was what
                 * re-escaped it. calaos_script decodes with a real parser
                 * (ScriptExtern_main.cpp), never by substring search, and both
                 * ends of this wire ship in the same package.
                 */
                Json jev = {{ "msg", "event" },
                            { "data", ev.toJson() }};
                process->sendMessage(ScriptWire::dumpJson(jev));
            }
        }));
    });

    string exe = Prefix::Instance().binDirectoryGet() + "/calaos_script";
    process->startProcess(exe, "lua");

    return process;
}
