/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
    if (ping_exe && ping_exe->referenced())
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
        ping_exe->close();

        lastStatus = ev.status == 0;

        cDebugDom("input") << "ping state is: " << lastStatus;

        int interval = 15000; //15s default interval
        if (Utils::is_of_type<int>(this->get_param("interval")))
            Utils::from_string(this->get_param("interval"), interval);

        Timer::singleShot(interval / 1000.0, sigc::mem_fun(*this, &PingInputSwitch::doPing));

        this->hasChanged();
    });
    ping_exe->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, auto &)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        ping_exe->close();
    });

    vector<string> tok;
    Utils::split(cmd, tok, " ");

    //convert args list to a char**
    const char **argarray = new const char*[tok.size() + 1];
    unsigned index = 1;
    for (auto it = tok.begin();it != tok.end();it++)
    {
        argarray[index] = it->c_str();
        index++;
    }
    argarray[index] = NULL;

    ping_exe->spawn(tok[0].c_str(), (char **)argarray);

    delete [] argarray;
}
