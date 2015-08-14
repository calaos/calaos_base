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
#include "PingInputSwitch.h"
#include "IOFactory.h"
#include "EcoreTimer.h"

using namespace Calaos;

REGISTER_INPUT(PingInputSwitch)

Eina_Bool PingInputSwitch_proc_del(void *data, int type, void *event);

PingInputSwitch::PingInputSwitch(Params &p):
    InputSwitch(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("PingInputSwitch");
    ioDoc->descriptionSet(_("A switch input based on the result of a ping command. Useful to detect presence of a host on the network."));
    ioDoc->paramAdd("host", _("IP address or host where to send the ping"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("timeout", _("Timeout of the ping request in ms"), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("interval", _("Interval between pings in ms. Default to 15 sec"), IODoc::TYPE_INT, false, "15000");
    ioDoc->conditionAdd("true", _("The host is online and respond to the ping"));
    ioDoc->conditionAdd("false", _("The host is offline and/or does not respond to the ping"));

    hProcDel = ecore_event_handler_add(ECORE_EXE_EVENT_DEL,
                                       PingInputSwitch_proc_del,
                                       this);

    doPing();
}

PingInputSwitch::~PingInputSwitch()
{
    ecore_event_handler_del(hProcDel);
    if (ping_exe)
    {
        ecore_exe_kill(ping_exe);
        ecore_exe_free(ping_exe);
    }
}

bool PingInputSwitch::readValue()
{
    return lastStatus;
}

void PingInputSwitch::doPing()
{
    string host = get_param("host");
    string timeoutVal = "";
    if (Utils::is_of_type<int>(get_param("timeout")))
        timeoutVal = "-W " + get_param("timeout");

    string cmd = "ping -c 1 " + timeoutVal + " " + host;
    cDebugDom("input") << "Starting ping: " << cmd;

    ping_exe = ecore_exe_run(cmd.c_str(), this);
}

Eina_Bool PingInputSwitch_proc_del(void *data, int type, void *event)
{
    VAR_UNUSED(type);
    Ecore_Exe_Event_Del *ev = reinterpret_cast<Ecore_Exe_Event_Del *>(event);
    PingInputSwitch *in = reinterpret_cast<PingInputSwitch *>(data);

    if (!data || !in ||
        in->ping_exe != ev->exe)
        return ECORE_CALLBACK_PASS_ON;

    in->ping_exe = nullptr;

    in->lastStatus = ev->exit_code == 0;

    cDebugDom("input") << "ping state is: " << in->lastStatus;

    int interval = 15000; //15s default interval
    if (Utils::is_of_type<int>(in->get_param("interval")))
        Utils::from_string(in->get_param("interval"), interval);

    EcoreTimer::singleShot(interval / 1000.0, sigc::mem_fun(*in, &PingInputSwitch::doPing));

    in->hasChanged();

    return ECORE_CALLBACK_RENEW;
}
