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
#include "InputTime.h"
#include "ListeRule.h"
#include "IOFactory.h"

using namespace Calaos;
using namespace Utils;

REGISTER_IO(InputTime)

InputTime::InputTime(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    with_date(false),
    value(false)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("InputTime");
    ioDoc->descriptionSet(_("Basic time input. An event is triggered when the current time equals the configured time. A specific date can also be set."));
    ioDoc->paramAdd("visible", _("A time object can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);

    ioDoc->paramAddInt("hour", _("Hour for this time input"), 0, 23, true);
    ioDoc->paramAddInt("min", _("Minutes for this time input"), 0, 59, true);
    ioDoc->paramAddInt("sec", _("Seconds for this time input"), 0, 59, true);

    ioDoc->paramAddInt("year", _("Year for this time input"), 0, 9999, false);
    ioDoc->paramAddInt("month", _("Month for this time input"), 1, 12, false);
    ioDoc->paramAddInt("day", _("Day for this time input"), 1, 31, false);

    ioDoc->conditionAdd("true", _("Event triggered when current time equals"));
    ioDoc->conditionAdd("false", _("Event triggered when current time is not equal"));
    ioDoc->conditionAdd("changed", _("Event on any change of time"));

    ListeRule::Instance().Add(this); //add this specific input to the EventLoop

    set_param("visible", "false");
    set_param("gui_type", "time");

    cDebugDom("input") << get_param("id");
}

InputTime::~InputTime()
{
}

void InputTime::hasChanged()
{
    if (!isEnabled()) return;

    bool val = false;

    if (get_params().Exists("year") && get_params().Exists("month") && get_params().Exists("day"))
    {
        with_date = true;

        from_string(get_param("year"), year);
        from_string(get_param("month"), month);
        from_string(get_param("day"), day);
    }
    else
    {
        with_date = false;
    }

    Utils::from_string(get_param("hour"), hour);
    Utils::from_string(get_param("min"), minute);
    Utils::from_string(get_param("sec"), second);

    {
        struct tm *ctime = NULL;
        tzset(); //Force reload of timezone data
        time_t t = time(NULL);
        ctime = localtime(&t);

        if (with_date)
        {
            if (ctime->tm_mday == day &&
                ctime->tm_mon + 1 == month &&
                ctime->tm_year + 1900 == year &&
                ctime->tm_sec == second &&
                ctime->tm_min == minute &&
                ctime->tm_hour == hour)
            {
                val = true;
            }
        }
        else
        {
            if (ctime->tm_sec == second &&
                ctime->tm_min == minute &&
                ctime->tm_hour == hour)
            {
                val = true;
            }
        }
    }

    if (val != value)
    {
        value = val;
        EmitSignalIO();

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "state", val?"true":"false" } });
    }
}
