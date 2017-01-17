/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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

#include <NTPClock.h>
#include "uvw/src/uvw.hpp"

using namespace std;

NTPClock & NTPClock::Instance()
{
    static NTPClock c;
    return c;
}

NTPClock::NTPClock():restartWhenApply(false), exe(NULL)
{
    sig_applyCalendar.connect(sigc::mem_fun(*this, &NTPClock::applyCalendarFromServer));
    IPC::Instance().AddHandler("CalaosCommon::NTPClock","applyCalendar", sig_applyCalendar,NULL);

    //the timer will be trigger when the ecore_loop will be run
    timer = new Timer(0.1, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
}

NTPClock::~NTPClock()
{
    if (timer)
    {
        delete timer;
        timer = NULL;
    }
}

void NTPClock::updateClock()
{
    if (Utils::get_config_option("use_ntp") == "true")
    {
        cInfo() <<  "NTPClock::updateClock() Updating clock...";

        string cmd = "/usr/sbin/ntpdate calaos.fr";

        exe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
        exe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &, auto &h)
        {
            h.close();
        });
        exe->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, auto &h)
        {
            cDebugDom("process") << "Process error: " << ev.what();
            h.close();
        });

        Utils::CStrArray arr(cmd);
        exe->spawn(arr.at(0), arr.data());
    }
}

void NTPClock::enable(bool en)
{
    if (en)
        Utils::set_config_option("use_ntp", "true");
    else
        Utils::set_config_option("use_ntp", "false");
}

void NTPClock::setRestartWhenApply(bool s)
{
    restartWhenApply = s;
}

bool NTPClock::isRestartWhenApply()
{
    return restartWhenApply;
}

void NTPClock::TimerTick()
{
    if (!timer) return;
    delete timer;
    timer = NULL;

    updateClock();

    timer = new Timer(60 * 60 * 12, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
}

void NTPClock::applyCalendarFromServer(string source, string s,
                                       void *listener_data, void* sender_data)
{
    if (networkCmdCalendarApply[2] == "ntp_on")
    {
        cInfo() <<  "Enabling NTP";
        enable(true);

        int timeZone;
        istringstream iss(networkCmdCalendarApply[3]);
        iss >> timeZone;

        cApply.timeZone.current = timeZone;
        cApply.applyTimezone();

        updateClock();
    }
    else if (networkCmdCalendarApply[2] == "ntp_off")
    {
        cInfo() <<  "Disabling NTP, update date and clock.";
        enable(false);

        {
            int i;
            istringstream iss(networkCmdCalendarApply[3]);
            iss >> i;
            cApply.setHours(i);
        }

        {
            int i;
            istringstream iss(networkCmdCalendarApply[4]);
            iss >> i;
            cApply.setMinutes(i);
        }

        {
            int i;
            istringstream iss(networkCmdCalendarApply[5]);
            iss >> i;
            cApply.setSecondes(i);
        }
        {
            int i;
            istringstream iss(networkCmdCalendarApply[6]);
            iss >> i;
            cApply.setDay(i);
        }

        {
            int i;
            istringstream iss(networkCmdCalendarApply[7]);
            iss >> i;
            cApply.setMonth(i);
        }

        {
            int i;
            istringstream iss(networkCmdCalendarApply[8]);
            iss >> i;
            cApply.setYear(i);
        }
        cApply.apply();
    }
}

void NTPClock::setNetworkCmdCalendarApply(vector < string > s)
{
    networkCmdCalendarApply = s;
}

void NTPClock::syncHwClock()
{
    syncexe = uvw::Loop::getDefault()->resource<uvw::ProcessHandle>();
    syncexe->once<uvw::ExitEvent>([this](const uvw::ExitEvent &, auto &h)
    {
        h.close();
        if (timer) delete timer;
        timer = new Timer(60 * 60 * 12, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
    });
    syncexe->once<uvw::ErrorEvent>([](const uvw::ErrorEvent &ev, auto &h)
    {
        cDebugDom("process") << "Process error: " << ev.what();
        h.close();
    });

    string cmd = "/sbin/hwclock --systohc";

    Utils::CStrArray arr(cmd);
    syncexe->spawn(arr.at(0), arr.data());
}
