/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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

#include <Utils.h>

using namespace Utils;

class TimeRange
{
private:
    //keep computed values in cache
    int cyear = 0, cmonth = 0, cday = 0;
    int sunrise_hour_cache = 0, sunrise_min_cache = 0;
    int sunset_hour_cache = 0, sunset_min_cache = 0;
    int dst_cache = false; //Daylight saving time

    long getTimezoneOffset();
    void computeSunSetRise(int year, int month, int day,
                           int &rise_hour, int &rise_min,
                           int &set_hour, int &set_min);

public:

    enum { BADDAY = -1, SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY };

    /* horaire type.
                 * When horaire type is HTYPE_NORMAL, the time properties are the real time.
                 * When HTYPE_SUNRISE, HTYPE_SUNSET or HTYPE_NOON are used, the time properties
                 * are used as an offset from the computed time.
                 */

    bool operator==(const TimeRange &other) const;
    bool operator!=(const TimeRange &other) const;

    enum { HTYPE_NORMAL = 0, HTYPE_SUNRISE, HTYPE_SUNSET, HTYPE_NOON };
    int start_type = HTYPE_NORMAL;
    int end_type = HTYPE_NORMAL;

    int start_offset = 1; //should be 1 or -1
    int end_offset = 1; //should be 1 or -1

    string shour, smin, ssec;
    string ehour, emin, esec;

    TimeRange();
    TimeRange(string proto);
    TimeRange(const Params &p);

    long getStartTimeSec(int year, int month, int day);
    long getEndTimeSec(int year, int month, int day);

    //compute value for today
    long getStartTimeSec();
    long getEndTimeSec();

    bool isSameStartEnd();

    string toProtoCommand(int day) const;
    Params toParams(int day) const;
    string toString();

    //flag to ease the loading in UI
    bitset<7> dayOfWeek;
};
