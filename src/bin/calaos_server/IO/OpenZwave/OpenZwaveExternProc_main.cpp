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

class OpenZwaveProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(OpenZwaveProcess)

protected:

    unsigned int universe = 0;

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);
};

void OpenZwaveProcess::readTimeout()
{
}

void OpenZwaveProcess::messageReceived(const string &msg)
{
    json_error_t jerr;
    json_t *jroot = json_loads(msg.c_str(), 0, &jerr);

    if (!jroot || !json_is_array(jroot))
    {
        cWarningDom("openzwave") << "Error parsing json from sub process: " << jerr.text;
        if (jroot)
            json_decref(jroot);
        return;
    }

    int idx;
    json_t *value;

    json_array_foreach(jroot, idx, value)
    {
        Params p;
        jansson_decode_object(value, p);

        if (p.Exists("channel") && p.Exists("value"))
        {
            unsigned int channel;
            unsigned int val;
            Utils::from_string(p["channel"], channel);
            Utils::from_string(p["value"], val);

            cDebugDom("openzwave") << "Set channel " << channel << " with value: " << val;
        }
    }
}

bool OpenZwaveProcess::setup(int &argc, char **&argv)
{
    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    if (argc >= 1)
        Utils::from_string(argv[1], universe);

    cDebug() << "Universe: " << universe;

    return true;
}

int OpenZwaveProcess::procMain()
{
    run();

    return 0;
}

EXTERN_PROC_CLIENT_MAIN(OpenZwaveProcess)
