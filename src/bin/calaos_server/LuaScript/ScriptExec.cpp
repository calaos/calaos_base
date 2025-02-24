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

ExternProcServer *ScriptExec::ExecuteScriptDetached(const string &script, std::function<void(bool ret)> cb, Params env)
{
    ExternProcServer *process = new ExternProcServer("lua");
    cInfoDom("lua") << "Starting script. (" << process << ")";

    JsonApi *jsonApi = new JsonApi();
    sigc::connection *evcon = new sigc::connection;

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

        cDebug() << "Process connected. process:" << process;
        Params p = {{ "msg", "execute" },
                    { "script", script } };

        json_t *jroot = p.toJson();

        //send the full calaos context here. (using JsonApi) to the process
        //after connect process to calaos events, and send him event so the process
        //can update its local cache of IO states.
        json_object_set_new(jroot, "context", jsonApi->buildFlatIOList());

        //Also append the env to the json. Actually the env can contain which io has triggered the script
        json_object_set_new(jroot, "env", env.toJson());

        string m = jansson_to_string(jroot);
        process->sendMessage(m);

        //After initial context, send all events to the external process
        *evcon = std::move(EventManager::Instance().newEvent.connect([=](const CalaosEvent &ev)
        {
            //only send IO events
            if (ev.getType() == CalaosEvent::EventIOAdded ||
                ev.getType() == CalaosEvent::EventIODeleted ||
                ev.getType() == CalaosEvent::EventIOChanged ||
                ev.getType() == CalaosEvent::EventIOPropertyDelete)
            {
                json_t *jev = json_object();
                json_object_set_new(jev, "msg", json_string("event"));
                json_object_set_new(jev, "data", ev.toJson());
                process->sendMessage(jansson_to_string(jev));
            }
        }));
    });

    string exe = Prefix::Instance().binDirectoryGet() + "/calaos_script";
    process->startProcess(exe, "lua");

    return process;
}
