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
#include <InputTimer.h>
#include <ListeRule.h>
#include <IPC.h>

using namespace Calaos;

InputTimer::InputTimer(Params &p):
                Input(p),
                Output(p),
                timer(NULL),
                value("true"),
                start(false)
{
        set_param("visible", "false");

        Utils::logger("input") << Priority::DEBUG << "InputTimer::InputTimer(" << get_param("id") << "): Ok" << log4cpp::eol;
}

InputTimer::~InputTimer()
{
        Utils::logger("input") << Priority::DEBUG << "InputTimer::~InputTimer(): Ok" << log4cpp::eol;
}

bool InputTimer::set_value(string command)
{
        if( command == "start")
                command = "true";

        if(command == "stop")
                command = "false";

        if (command == "true")
        {
                Utils::logger("output") << Priority::INFO << "InputTimer(" << get_param("id") << "): got action, Start Timer" << log4cpp::eol;
                StartTimer();
        }
        else if(command == "false")
        {
                Utils::logger("output") << Priority::INFO << "InputTimer(" << get_param("id") << "): got action, Stop Timer" << log4cpp::eol;
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
                                Utils::logger("output") << Priority::WARN <<
                                        "InputTimer: Invalid time value: " <<j<< log4cpp::eol;
                }

                set_param("hour",to_string(hour));
                set_param("min",to_string(minute));
                set_param("sec",to_string(second));
                set_param("msec",to_string(ms));

                // Restart timer if it was running
                if(timer)
                        StartTimer();
        }

        if(command == "true" || command == "false")
        {
                string sig = "input ";
                sig += get_param("id") + " ";
                sig += Utils::url_encode("state:");
                sig += Utils::url_encode(value);
                IPC::Instance().SendEvent("events", sig);
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
        timer = new EcoreTimer((double)msec / 1000.,
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

        EmitSignalInput();

        string sig = "input ";
        sig += get_param("id") + " ";
        sig += Utils::url_encode("state:true");
        IPC::Instance().SendEvent("events", sig);
}

void InputTimer::hasChanged()
{

}
