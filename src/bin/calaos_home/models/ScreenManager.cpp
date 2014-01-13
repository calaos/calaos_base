/******************************************************************************
**  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include "ScreenManager.h"
#include <XUtils.h>

ScreenManager::ScreenManager()
{
        //disable the DPMS
        XUtils::UpdateDPMS(false, 0);
}

ScreenManager::~ScreenManager()
{
        stopTimer();
}

int ScreenManager::getTime()
{
        string time = Utils::get_config_option("dpms_standby");
        int t;
        Utils::from_string(time, t);

        return t;
}

void ScreenManager::updateTimer()
{
        if (is_suspended)
        {
                wakeUp();
        }
        else
        {
                stopTimer();
                startTimer();
        }
}

void ScreenManager::startTimer()
{
        if (timer) return;

        timer = new EcoreTimer(getTime(), sigc::slot<void>([this]()
        {
                if(Utils::get_config_option("dpms_enable") == "true")
                        suspend();

                stopTimer();
        }));
}

void ScreenManager::stopTimer()
{
        DELETE_NULL(timer);
}

void ScreenManager::wakeUp()
{
        if(!is_suspended) return;
        wakeup_screen_start.emit();
}

void ScreenManager::wakeUpNow()
{
        if (!is_suspended) return;
        is_suspended = false;

        //Sometimes the screen wakes up but it remain black
        //doing ON->OFF->ON prevents this
        XUtils::WakeUpScreen(true);
        XUtils::WakeUpScreen(false);
        XUtils::WakeUpScreen(true);

        startTimer();
        wakeup_screen.emit();

        XUtils::UpdateDPMS(false, 0);
}

void ScreenManager::wakeUpNowWhenScreenOn()
{
        if (!is_suspended) return;
        is_suspended = false;

        startTimer();
        wakeup_screen.emit();
}

void ScreenManager::suspendNow()
{
        if (is_suspended) return;
        is_suspended = true;

        XUtils::UpdateDPMS(true, 0);
        XUtils::WakeUpScreen(false);

        stopTimer();
        suspend_screen.emit();
}

void ScreenManager::suspend()
{
        if (is_suspended) return;
        suspend_screen_start.emit();
}
