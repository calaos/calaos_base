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
#include "ScriptWire.h"

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
    Json jroot;

    //E4.1j: the parser error text the previous C library filled in here has
    //no equivalent on a non throwing Json::parse(). The raw message is still
    //logged, which is what a reader needs; same choice as the Wago and
    //Reolink wires.
    if (!ScriptWire::parseMessage(msg, jroot))
    {
        cWarningDom("lua") << "Error parsing json from sub process. Raw message: " << msg;
        return;
    }

    string mtype = ScriptWire::stringGet(jroot, "msg");

    if (mtype == "execute")
    {
        cInfoDom("lua") << "(PID#" << getpid() << ") " << "Executing LUA script";
        string script = ScriptWire::stringGet(jroot, "script");

        ScriptManager::Instance().luaCalaos.setExternProcClient(this);

        //"context" is an ARRAY of IO objects, and its order is preserved
        const vector<Params> ios = ScriptWire::decodeContext(jroot);
        for (size_t i = 0;i < ios.size();i++)
        {
            LuaIOBase io(this);
            io.params = ios[i];
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
        ScriptWire::decodeEnv(jroot, ScriptManager::Instance().luaCalaos.env);

        //Execute the script, this call will block
        bool ret = ScriptManager::Instance().ExecuteScript(script);

        sendMessage(ScriptWire::buildFinishedMessage(ret));

        cInfoDom("lua") << "(PID#" << getpid() << ") " << "Script finished, exiting process.";

        ::exit(0);
    }
    else if (mtype == "event")
    {
        //decodeEvent() takes jroot by CONST reference on purpose: the walk
        //goes two levels down a document it does not own, the previous C
        //accessor answered NULL on a missing "data" without touching
        //anything, and the nlohmann operator[] would CREATE the key instead.
        //See ScriptWire.h.
        Params ev;
        string t;
        ScriptWire::decodeEvent(jroot, ev, t);

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
