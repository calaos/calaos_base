/******************************************************************************
**  Copyright (c) 2006-2013, Calaos. All Rights Reserved.
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

#include <Utils.h>

using namespace Utils;

enum { BADDAY = -1, SUNDAY = 0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY };

class TimeRange
{
        private:
                //keep computed values in cache
                int cyear, cmonth, cday;
                int sunrise_hour_cache, sunrise_min_cache;
                int sunset_hour_cache, sunset_min_cache;

                long getTimezoneOffset();
                void computeSunSetRise(int year, int month, int day,
                                       int &rise_hour, int &rise_min,
                                       int &set_hour, int &set_min);

        public:

                /* horaire type.
                 * When horaire type is HTYPE_NORMAL, the time properties are the real time.
                 * When HTYPE_SUNRISE, HTYPE_SUNSET or HTYPE_NOON are used, the time properties
                 * are use as an offset form the computed time.
                 */

                enum { HTYPE_NORMAL = 0, HTYPE_SUNRISE, HTYPE_SUNSET, HTYPE_NOON };
                int start_type;
                int end_type;

                int start_offset; //should be 1 or -1
                int end_offset; //should be 1 or -1

                string shour, smin, ssec;
                string ehour, emin, esec;

                TimeRange();

                long getStartTimeSec(int year, int month, int day);
                long getEndTimeSec(int year, int month, int day);
};
