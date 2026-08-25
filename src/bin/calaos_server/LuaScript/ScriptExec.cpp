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
        json_error_t jerr;
        json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

        if (!jroot || !json_is_object(jroot))
        {
            cWarningDom("lua") << "Error parsing json from sub process: " << jerr.text << " Raw message: " << msg;
            if (jroot)
                json_decref(jroot);
            return;
        }

        string mtype = jansson_string_get(jroot, "msg");

        if (mtype == "finished")
        {
            cInfoDom("lua") << "LUA script finished.";
            process->terminate();
            string ret = jansson_string_get(jroot, "return_val", "false");
            processStatus[process] = ProcessFinished;
            cb(ret == "true"); //process finished, call callback, process will be deleted later
        }
        else if (mtype == "set_state")
        {
            Params p;
            jansson_decode_object(json_object_get(jroot, "data"), p);
            if (!jsonApi->decodeSetState(p))
                cWarningDom("lua") << "Failed to decode set_state from Lua Script!";
        }
        else if (mtype == "set_param")
        {
            Params p;
            jansson_decode_object(json_object_get(jroot, "data"), p);
            if (!jsonApi->buildJsonSetParam(p))
                cWarningDom("lua") << "Failed to decode set_param from Lua Script!";
        }
        else if (mtype == "send_push_notif")
        {
            Params p;
            jansson_decode_object(json_object_get(jroot, "data"), p);

            ActionPush *push = new ActionPush(p["message"], p["attachment"]);
            push->notifSent.connect([push]()
            {
                cDebugDom("lua") << "ActionPush finished, deleting...";
                delete push;
            });
            push->Execute();
        }

        json_decref(jroot);
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

        json_t *jroot = jansson_from_params(p);

        //send the full calaos context here. (using JsonApi) to the process
        //after connect process to calaos events, and send him event so the process
        //can update its local cache of IO states.
        json_object_set_new(jroot, "context", jsonApi->buildFlatIOList());

        //Also append the env to the json. Actually the env can contain which io has triggered the script
        json_object_set_new(jroot, "env", jansson_from_params(env));

        string m = jansson_to_string(jroot);
        process->sendMessage(m);

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
                //E4.1l: the ONE call site of the transitional adapter (see
                //Jansson_Addition.h). This message stays jansson until E4.1m
                //migrates this file, because its sibling above carries
                //JsonApi::buildFlatIOList(), still a json_t *. The adapter
                //keeps the bytes of this wire as close as they can be: jansson
                //re-escapes in its own form, so only the key order of the
                //event object moves, and calaos_script decodes with a real
                //parser (ScriptExtern_main.cpp), never by substring search.
                json_t *jev = json_object();
                json_object_set_new(jev, "msg", json_string("event"));
                json_object_set_new(jev, "data", jansson_from_json(ev.toJson()));
                process->sendMessage(jansson_to_string(jev));
            }
        }));
    });

    string exe = Prefix::Instance().binDirectoryGet() + "/calaos_script";
    process->startProcess(exe, "lua");

    return process;
}
