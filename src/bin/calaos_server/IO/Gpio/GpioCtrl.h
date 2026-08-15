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
#ifndef GPIOCTRL_H
#define GPIOCTRL_H

#include <Utils.h>
#include <Timer.h>

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class PipeHandle;
}

namespace Calaos
{

class GpioCtrl
{
private:
    int gpionum; // GPIO Number
    string gpionum_str;
    int writeFile(string path, string value);
    int readFile(string path, string &value);
    int fd;
    sigc::connection connection;
    sigc::signal<void> event_signal;
    std::shared_ptr<uvw::PipeHandle> fdHandle;
    bool debounce;
    double debounce_time;

    //Lifetime token: async callbacks (debounce timer) capture a weak_ptr
    //to it and bail out if the GpioCtrl was deleted in the meantime.
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);

public:
    //Fallback (the historical hardcoded value) and upper sanity bound
    //for the configurable input debounce time, in seconds.
    static constexpr double DEBOUNCE_TIME_DEFAULT = 0.05;
    static constexpr double DEBOUNCE_TIME_MAX = 5.0;

    //True when v is a usable debounce time: > 0 and <= DEBOUNCE_TIME_MAX
    //seconds. NaN fails both comparisons and is rejected too.
    static bool debounceTimeValid(double v)
    {
        return v > 0.0 && v <= DEBOUNCE_TIME_MAX;
    }

    /* Non-throwing bounded parse of a debounce_time config value
     * (pattern: parseGridDimension, T1.10).
     * Beware: Utils::from_string("") returns true with a zero-filled
     * dest, so an absent param surfaces as 0 here and is caught by the
     * range check, like any other invalid value.
     * Header-inline so it can be unit-tested without linking server
     * objects. */
    static double parseDebounceTime(const std::string &value,
                                    double fallback = DEBOUNCE_TIME_DEFAULT)
    {
        double v = 0.0;
        if (!Utils::from_string(value, v))
            return fallback;
        return debounceTimeValid(v)? v : fallback;
    }

    GpioCtrl(int _gpionum, double _debounce_time = DEBOUNCE_TIME_DEFAULT);
    ~GpioCtrl();
    bool exportGpio();
    bool unexportGpio();
    bool setDirection(string direction);
    bool setEdge(string direction);
    bool setActiveLow(bool active_low);
    bool setVal(bool value);
    bool getVal(bool &value);
    void closeFd(void);
    int getGpioNum(void);
    bool setValueChanged(sigc::slot<void> slot);
    void emitChange();
};
}
#endif // GPIOCTRL_H
