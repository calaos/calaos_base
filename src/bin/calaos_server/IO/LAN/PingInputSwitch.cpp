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
#include "PingInputSwitch.h"
#include "IOFactory.h"
#include "Timer.h"
#include "libuvw.h"

using namespace Calaos;

REGISTER_IO(PingInputSwitch)

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

    if (!param_exists("interval")) set_param("interval", "15000");

    if (param_exists("host")) doPing();
}

PingInputSwitch::~PingInputSwitch()
{
    //The timer holds a slot bound to this object. It must not outlive it,
    //or the next tick would call doPing() on freed memory (config reload).
    DELETE_NULL(pollTimer);

    //Only signal a process that is really running: a handle whose spawn
    //failed keeps pid 0, and killing 0 signals our own process group. The
    //exit and error handlers already closed the handle in every other case.
    if (ping_exe && pingRunning && ping_exe->pid() > 0)
    {
        ping_exe->kill(SIGTERM);
        ping_exe->close();
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

    ping_exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    ping_exe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &ev, auto &)
    {
        pingRunning = false;
        ping_exe->close();

        lastStatus = ev.status == 0;

        cDebugDom("input") << "ping state is: " << lastStatus;

        int interval = 15000; //15s default interval
        if (Utils::is_of_type<int>(this->get_param("interval")))
            Utils::from_string(this->get_param("interval"), interval);

        //Timer::singleShot() would survive this object and tick into freed
        //memory. Keep the timer as a member instead, ~PingInputSwitch()
        //destroys it.
        DELETE_NULL(pollTimer);
        pollTimer = new Timer(interval / 1000.0,
                              sigc::mem_fun(*this, &PingInputSwitch::pollTimeout));

        this->hasChanged();
    });
    ping_exe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        pingRunning = false;
        ping_exe->close();
    });

    vector<string> tok;
    Utils::split(cmd, tok, " ");

    if (tok.empty())
    {
        cErrorDom("input") << "Empty ping command, not spawning anything";
        return;
    }

    //convert args list to a char**: argv[0] is the program itself, and the
    //array is NULL terminated, so it holds tok.size() + 1 entries.
    vector<const char *> argarray;
    argarray.reserve(tok.size() + 1);
    for (const auto &arg: tok)
        argarray.push_back(arg.c_str());
    argarray.push_back(nullptr);

    ping_exe->spawn(tok[0].c_str(), (char **)argarray.data());

    //spawn() reports a failure through ErrorEvent, published synchronously,
    //and leaves pid at 0 in that case
    pingRunning = ping_exe->pid() > 0;
}

void PingInputSwitch::pollTimeout()
{
    //The timer is one shot: destroy it before starting the next ping, the
    //exit handler creates a new one when the ping is done.
    DELETE_NULL(pollTimer);
    doPing();
}
