
/******************************************************************************
 **  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos Home.
 **
 **  Calaos Home is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos Home is distributed in the hope that it will be useful,
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

using namespace std;

static Eina_Bool _NTPHandle1(void *data, int type, void *event)
{
        NTPClock *ntpClock = reinterpret_cast < NTPClock * >(data);
        if (ntpClock)
                ntpClock->Handle1();

        return 0;
}

static Eina_Bool _NTPHandle2(void *data, int type, void *event)
{
        NTPClock *ntpClock = reinterpret_cast < NTPClock * >(data);
        if (ntpClock)
                ntpClock->Handle2();

        return 0;
}

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
        timer = new EcoreTimer(0, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
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
        if (exe)
                return;

        if (Utils::get_config_option("use_ntp") == "true")
        {
                handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _NTPHandle1, this);
                cInfoDom("root") <<  "NTPClock::updateClock() Updating clock...";

                string cmd = "/usr/sbin/ntpdate calaos.fr";

                exe = ecore_exe_run(cmd.c_str(), NULL);
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

        timer = new EcoreTimer(60 * 60 * 12, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
}

void NTPClock::applyCalendarFromServer(string source, string s,
                void *listener_data, void* sender_data)
{
        if (networkCmdCalendarApply[2] == "ntp_on")
        {
                cInfoDom("root") <<  "Enabling NTP";
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
                cInfoDom("root") <<  "Disabling NTP, update date and clock.";
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

void NTPClock::Handle1()
{
        string cmd = "/sbin/hwclock --systohc";

        ecore_event_handler_del(handler);
        handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _NTPHandle2, this);
        exe = ecore_exe_run(cmd.c_str(), NULL);
}

void NTPClock::Handle2()
{
        ecore_event_handler_del(handler);

        cInfoDom("root") <<  "NTPClock: Updating clock...  DONE";

        exe = NULL;
        //kill l'application
        //elle sera redémarrée par un daemon extérieur

        if (isRestartWhenApply())
        {
                cInfoDom("root") <<  "NTPClock: Restart the application";
                pid_t pid = getpid();
                kill(pid, SIGKILL);
        }
        else
        {
                if (timer) delete timer;
                timer = new EcoreTimer(60 * 60 * 12, (sigc::slot<void>)sigc::mem_fun(*this, &NTPClock::TimerTick));
        }
}
