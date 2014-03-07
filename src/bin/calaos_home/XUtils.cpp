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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <XUtils.h>

//for DPMS
#ifdef HAVE_ECORE_X
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#endif

void XUtils::UpdateDPMS(bool enable, int seconds)
{
#ifdef HAVE_ECORE_X
    //X11 display
    Display *x_display;

    x_display = XOpenDisplay(":0");
    if (!x_display)
    {
        cWarning() <<  "XUtils::UpdateDPMS(): trying with $DISPLAY";
        x_display = XOpenDisplay(NULL);

        if (!x_display)
        {
            cError() <<  "XUtils::UpdateDPMS(): Error opening X11 display";
            return;
        }
    }

    if (!DPMSCapable(x_display))
    {
        cWarning() <<  "X display is not DPMS capable !";
    }
    else
    {
        if (enable)
        {
            DPMSEnable(x_display);

            int s1 = seconds - 4, s2 = seconds - 2, s3 = seconds;
            if (s1 < 0) s1 = 0;
            if (s2 < 0) s2 = 0;
            if (s3 < 0) s3 = 0;

            //set timeouts
            DPMSSetTimeouts(x_display, s1, s2, s3);
        }
        else
            DPMSDisable(x_display);

        XSetScreenSaver(x_display, 0, 0, 0, 0);
    }

    //Close the X11 connection
    if (x_display)
        XCloseDisplay(x_display);
#endif
}

void XUtils::WakeUpScreen(bool enable)
{
#ifdef HAVE_ECORE_X
    //X11 display
    Display *x_display;

    x_display = XOpenDisplay(":0");
    if (!x_display)
    {
        cWarning() <<  "XUtils::WakeUpScreen(): trying with $DISPLAY";
        x_display = XOpenDisplay(NULL);

        if (!x_display)
        {
            cError() <<  "XUtils::WakeUpScreen(): Error opening X11 display";
            return;
        }
    }

    if (!DPMSCapable(x_display))
    {
        cWarning() <<  "X display is not DPMS capable !";
    }
    else
    {
        if (enable)
        {
            DPMSForceLevel(x_display, DPMSModeOn);
        }
        else
        {
            DPMSForceLevel(x_display, DPMSModeOff);
        }
    }

    //Close the X11 connection
    if (x_display)
        XCloseDisplay(x_display);
#endif
}

int XUtils::getDPMSInfo()
{
#ifdef HAVE_ECORE_X
    //X11 display
    Display *x_display;
    int ret = DPMS_NOTAVAILABLE;

    x_display = XOpenDisplay(":0");
    if (!x_display)
        cError() <<  "XUtils::getDPMSInfo(): Error opening X11 display";

    if (!DPMSCapable(x_display))
    {
        cWarning() <<  "X display is not DPMS capable !";
    }
    else
    {
        CARD16 mode;
        BOOL state;
        DPMSInfo(x_display, &mode, &state);

        if (!state)
            ret = DPMS_DISABLED;
        else
        {
            if (mode == DPMSModeOn) ret = DPMS_ON;
            if (mode == DPMSModeOff) ret = DPMS_OFF;
            if (mode == DPMSModeStandby) ret = DPMS_STANDBY;
            if (mode == DPMSModeSuspend) ret = DPMS_SUSPEND;
        }
    }

    //Close the X11 connection
    if (x_display)
        XCloseDisplay(x_display);

    return ret;
#endif
    return 0;
}

