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
#ifndef SREENMANAGER_H
#define SREENMANAGER_H

#include <Utils.h>
#include <EcoreTimer.h>

/*
 * This class is used to manage the screen suspend option and display
 **/

class ScreenManager
{
private:
    EcoreTimer *timer = nullptr;
    bool is_suspended = false;

    ScreenManager();

public:
    static ScreenManager& instance()
    {
        static ScreenManager s;
        return s;
    }

    ~ScreenManager();

    int getTime();

    // Re-init the timer to 0, if the timer reach the time returns by getTime(), the screen will be suspend
    void updateTimer();

    void startTimer();
    void stopTimer();
    void wakeUp();
    void suspend();

    void wakeUpNow();
    void suspendNow();

    // Wake up when the screen is on, avoid to flash the screen */
    void wakeUpNowWhenScreenOn();

    //signals for views
    sigc::signal<void> wakeup_screen_start;
    sigc::signal<void> wakeup_screen;
    sigc::signal<void> suspend_screen_start;
    sigc::signal<void> suspend_screen;
};

#endif // SREENMANAGER_H
