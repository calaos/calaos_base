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
#include "ExternProc.h"
#include "LuaScript/ScriptManager.h"

using namespace Calaos;

class ScriptProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(ScriptProcess)

protected:

    map<string, string> waitIds;

    //needs to be reimplemented
    virtual void readTimeout() {}
    virtual void messageReceived(const string &msg);
};

void ScriptProcess::messageReceived(const string &msg)
{
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

    if (mtype == "execute")
    {
        cInfoDom("lua") << "(PID#" << getpid() << ") " << "Executing LUA script";
        string script = jansson_string_get(jroot, "script");

        json_t *jctx = json_object_get(jroot, "context");
        size_t idx;
        json_t *value;

        ScriptManager::Instance().luaCalaos.setExternProcClient(this);

        json_array_foreach(jctx, idx, value)
        {
            LuaIOBase io(this);
            jansson_decode_object(value, io.params);
            ScriptManager::Instance().luaCalaos.ioMap[io.params["id"]] = io;
        }

        //This gets executed on every line call
        ScriptManager::Instance().debugHook.connect([=]()
        {
            fd_set events;
            struct timeval tv{0,0};
            FD_ZERO(&events);
            FD_SET(getSocketFd(), &events);
            int ret = select(getSocketFd() + 1, &events, NULL, NULL, &tv);
            if (ret > 0)
            {
                if (FD_ISSET(getSocketFd(), &events))
                {
                    if (!processSocketRecv())
                    {
                        cErrorDom("lua") << "Failed to process ExternProc socket";
                        ScriptManager::Instance().abortScript();
                    }
                }
            }
            else if (ret < 0)
                ScriptManager::Instance().abortScript();
        });

        ScriptManager::Instance().luaCalaos.waitForIOChanged.connect([=](const string &id) -> bool
        {
            waitIds.clear();

            //start a mainloop around select now, the script is paused
            fd_set events;
            FD_ZERO(&events);
            FD_SET(getSocketFd(), &events);
            int ret = select(getSocketFd() + 1, &events, NULL, NULL, NULL);
            if (ret > 0)
            {
                if (FD_ISSET(getSocketFd(), &events))
                {
                    if (!processSocketRecv())
                    {
                        cErrorDom("lua") << "Failed to process ExternProc socket";
                        ScriptManager::Instance().abortScript();
                    }
                }
            }
            else if (ret < 0)
                ScriptManager::Instance().abortScript();

            cDebug() << "waitIds.size: " << waitIds.size();
            cDebug() << "waitIds.find(" << id << "): " << (waitIds.find(id) != waitIds.end()?"true":"false");

            //return true if IO has been changed, false otherwise
            return waitIds.find(id) != waitIds.end();
        });

        //Set env
        jansson_decode_object(json_object_get(jroot, "env"),
                              ScriptManager::Instance().luaCalaos.env);

        //Execute the script, this call will block
        bool ret = ScriptManager::Instance().ExecuteScript(script);

        Params pret = {{ "msg", "finished" },
                       { "return_val", ret?"true":"false" }};
        sendMessage(jansson_to_string(pret.toJson()));

        cInfoDom("lua") << "(PID#" << getpid() << ") " << "Script finished, exiting process.";

        ::exit(0);
    }
    else if (mtype == "event")
    {
        json_t *jev = json_object_get(jroot, "data");
        json_t *jdata = nullptr;
        if (jev)
            jdata = json_object_get(jev, "data");
        Params ev;
        jansson_decode_object(jdata, ev);

        string t = jansson_string_get(jev, "type_str");

        //this IO has been changed by an event
        //if script is waiting on that IO, the script will be resumed
        waitIds[ev["id"]] = ev["id"];

        if (ScriptManager::Instance().luaCalaos.ioMap.find(ev["id"]) !=
            ScriptManager::Instance().luaCalaos.ioMap.end())
        {
            if (t == "io_deleted")
            {
                ScriptManager::Instance().luaCalaos.ioMap.erase(ev["id"]);
            }
            else if (t == "io_changed")
            {
                for (int i = 0;i < ev.size();i++)
                {
                    string key, val;
                    ev.get_item(i, key, val);
                    ScriptManager::Instance().luaCalaos.ioMap[ev["id"]].params.Add(key, val);
                }
            }
            else if (t == "io_prop_deleted")
            {
                //TODO
            }
        }

        if (t == "io_added")
        {
            LuaIOBase io(this);
            io.params = ev;
            ScriptManager::Instance().luaCalaos.ioMap[ev["id"]] = io;
        }
    }

    json_decref(jroot);
}

bool ScriptProcess::setup(int &argc, char **&argv)
{
    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    return true;
}

int ScriptProcess::procMain()
{
    run();

    return 0;
}

EXTERN_PROC_CLIENT_MAIN(ScriptProcess)
