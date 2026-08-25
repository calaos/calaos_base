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
#include "OLAWire.h"

#include <ola/DmxBuffer.h>
#include <ola/Logging.h>
#include <ola/StreamingClient.h>

class OLAProcess: public ExternProcClient
{
public:

    //needs to be reimplemented
    virtual bool setup(int &argc, char **&argv);
    virtual int procMain();

    EXTERN_PROC_CLIENT_CTOR(OLAProcess)

protected:

    ola::DmxBuffer buffer;
    ola::StreamingClient client;
    unsigned int universe = 0;

    //needs to be reimplemented
    virtual void readTimeout();
    virtual void messageReceived(const string &msg);
};

void OLAProcess::readTimeout()
{
}

/*
 * E4.1f: the decoding lives in OLAWire.h, which OLACtrl.cpp includes too, so
 * both ends of the wire read from the same text and tests/OLAWire_test.cpp
 * exercises the SHIPPED decoder.
 *
 * ⚠️ THE ROOT IS AN ARRAY. decodeMessage() answers false exactly where
 * json_loads() answered NULL or json_is_array() was false, so a malformed
 * message, a top level scalar and a top level OBJECT are all refused here
 * and the buffer is NOT sent - unchanged.
 *
 * The only observable difference is the log line: nlohmann's non-throwing
 * parse has no jerr.text to offer, so the raw message is logged instead,
 * exactly as KNXExternProc_main.cpp does since E4.1e.
 */
void OLAProcess::messageReceived(const string &msg)
{
    vector<OLAWire::ChannelValue> entries;

    if (!OLAWire::decodeMessage(msg, entries))
    {
        cWarningDom("ola") << "Error parsing json from sub process. Raw message: " << msg;
        return;
    }

    for (const OLAWire::ChannelValue &entry: entries)
    {
        cDebugDom("ola") << "Set channel " << entry.channel << " with value: " << entry.value;
        buffer.SetChannel(entry.channel, entry.value);
    }

    client.SendDmx(universe, buffer);
}

bool OLAProcess::setup(int &argc, char **&argv)
{
    if (!connectSocket())
    {
        cError() << "process cannot connect to calaos_server";
        return false;
    }

    //argv[1] is only valid when argc > 1, otherwise we would dereference
    //the NULL terminator of argv
    if (argc > 1)
        Utils::from_string(argv[1], universe);

    cDebug() << "Universe: " << universe;
    ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);

    //set all channel to 0
    buffer.Blackout();

    //connect client to olad
    if (!client.Setup())
    {
        cError() << "Unable to connect to OLA server";
        return false;
    }
    cInfo() << "OLA connect ok";

    return true;
}

int OLAProcess::procMain()
{
    run();

    //disconnect ola client
    client.Stop();

    return 0;
}

EXTERN_PROC_CLIENT_MAIN(OLAProcess)
