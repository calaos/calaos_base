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
#include "InputTimer.h"
#include "ListeRule.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(InputTimer)

InputTimer::InputTimer(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    timer(NULL),
    value("true"),
    start(false)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("InputTimer");
    ioDoc->descriptionSet(_("Timer object. trigger an event after the configured time has expired."));
    ioDoc->paramAdd("visible", _("A timer object can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);

    ioDoc->paramAddInt("hour", _("Hour for the timer interval"), 0, 23, true);
    ioDoc->paramAddInt("min", _("Minutes for the timer interval"), 0, 59, true);
    ioDoc->paramAddInt("sec", _("Seconds for the timer interval"), 0, 59, true);
    ioDoc->paramAddInt("msec", _("Miliseconds for the timer interval"), 0, 999, true);
    ioDoc->paramAdd("autostart", _("Auto start the timer when calaos starts"), IODoc::TYPE_BOOL, true);
    ioDoc->paramAdd("autorestart", _("Auto restart the timer when time expires"), IODoc::TYPE_BOOL, true);

    ioDoc->conditionAdd("true", _("Event triggered when timer expires"));
    ioDoc->conditionAdd("false", _("Event triggered when timer starts"));
    ioDoc->conditionAdd("change", _("Event triggered on any change"));
    ioDoc->actionAdd("start", _("Start the timer"));
    ioDoc->actionAdd("stop", _("Stop the timer"));
    ioDoc->actionAdd("00:00:00:200", _("Reset the configured time to a value. Format is h:m:s:ms"));

    set_param("visible", "false");
    set_param("gui_type", "timer");

    if (get_param("autostart") == "true")
        StartTimer();

    cDebugDom("input") << get_param("id") << ": Ok";
}

InputTimer::~InputTimer()
{
}

bool InputTimer::set_value(string command)
{
    if (!isEnabled()) return true;

    if( command == "start")
        command = "true";

    if(command == "stop")
        command = "false";

    if (command == "true")
    {
        cInfoDom("output") << get_param("id") << ": got action, Start Timer";
        StartTimer();
    }
    else if(command == "false")
    {
        cInfoDom("output") << get_param("id") << ": got action, Stop Timer";
        StopTimer();
    }
    else
    {
        //set the time
        vector<string> valSplit;
        vector<string>::reverse_iterator it;
        split(command, valSplit, ":");
        int i = 0;

        ms = 0;
        second = 0;
        minute = 0;
        hour = 0;

        for(it = valSplit.rbegin(); it != valSplit.rend(); it++)
        {
            int j;
            if( from_string(*it, j) )
            {
                if(i==0)
                    ms = j;
                else if(i==1)
                    second = j;
                else if(i==2)
                    minute = j;
                else
                    hour = j;
                i++;
            }
            else
                cWarningDom("output") << "Invalid time value: " << j;
        }

        set_param("hour", Utils::to_string(hour));
        set_param("min", Utils::to_string(minute));
        set_param("sec", Utils::to_string(second));
        set_param("msec", Utils::to_string(ms));

        // Restart timer if it was running
        if(timer)
            StartTimer();
    }

    if(command == "true" || command == "false")
    {
        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "state", value } });
    }

    return true;
}

void InputTimer::StartTimer()
{
    long int msec = 0;
    Utils::from_string(get_param("hour"), hour);
    Utils::from_string(get_param("min"), minute);
    Utils::from_string(get_param("sec"), second);
    Utils::from_string(get_param("msec"), ms);
    msec = second + minute * 60 + hour * 3600;
    msec *= 1000; //in milisecond
    msec += ms;

    //security check: do not allow timer lower than 50 ms !
    if (msec < 50) msec = 50;

    if (timer) delete timer;
    timer = new Timer((double)msec / 1000.,
                           (sigc::slot<void>)sigc::mem_fun(*this, &InputTimer::TimerDone));

    value = "false";
}

void InputTimer::StopTimer()
{
    if (timer) delete timer;
    timer = NULL;
    value = "false";
}

void InputTimer::TimerDone()
{
    if (timer) delete timer;
    timer = NULL;
    value = "true";

    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", "true" } });

    if (!isEnabled()) return;

    if (get_param("autorestart") == "true")
        StartTimer();
}

void InputTimer::hasChanged()
{

}
