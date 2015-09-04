/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
        cWarningDom("knx") << "Error parsing json from sub process: " << jerr.text << " Raw message: " << msg;
        if (jroot)
            json_decref(jroot);
        return;
    }

    string mtype = jansson_string_get(jroot, "type");

    if (mtype == "execute")
    {
        cInfoDom("lua") << "Executing LUA script";
        string script = jansson_string_get(jroot, "script");

        json_t *jctx = json_object_get(jroot, "context");
        //TODO: handle calaos context here

        //ScriptManager::Instance().ExecuteScript(script);
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
