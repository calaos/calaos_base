/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <TimeRange.h>
#include <sys/time.h>

#include "sunset.h"

TimeRange::TimeRange():
    shour("0"), smin("0"), ssec("0"),
    ehour("0"), emin("0"), esec("0")
{
}

TimeRange::TimeRange(std::string proto):
    shour("0"), smin("0"), ssec("0"),
    ehour("0"), emin("0"), esec("0")
{
    std::vector<std::string> tokens;
    Utils::split(proto, tokens, ":", 11);

    shour = tokens[1];
    smin = tokens[2];
    ssec = tokens[3];
    Utils::from_string(tokens[4], start_type);
    Utils::from_string(tokens[5], start_offset);
    if (start_offset < 0) start_offset = -1;
    if (start_offset >= 0) start_offset = 1;

    ehour = tokens[6];
    emin = tokens[7];
    esec = tokens[8];
    Utils::from_string(tokens[9], end_type);
    Utils::from_string(tokens[10], end_offset);
    if (end_offset < 0) end_offset = -1;
    if (end_offset >= 0) end_offset = 1;
}

TimeRange::TimeRange(const Params &p)
{
    shour = p["start_hour"];
    smin = p["start_min"];
    ssec = p["start_sec"];
    Utils::from_string(p["start_type"], start_type);
    Utils::from_string(p["start_offset"], start_offset);
    if (start_offset < 0) start_offset = -1;
    if (start_offset >= 0) start_offset = 1;

    ehour = p["end_hour"];
    emin = p["end_min"];
    esec = p["end_sec"];
    Utils::from_string(p["end_type"], end_type);
    Utils::from_string(p["end_offset"], end_offset);
    if (end_offset < 0) end_offset = -1;
    if (end_offset >= 0) end_offset = 1;
}

bool TimeRange::operator==(const TimeRange &other) const
{
    return (start_type == other.start_type &&
            end_type == other.end_type &&
            start_offset == other.start_offset &&
            end_offset == other.end_offset &&
            shour == other.shour &&
            smin == other.smin &&
            ssec == other.ssec &&
            ehour == other.ehour &&
            emin == other.emin &&
            esec == other.esec);
}

bool TimeRange::operator!=(const TimeRange &other) const
{
    return !(*this == other);
}

long TimeRange::getStartTimeSec(int year, int month, int day)
{
    long v = 0;

    if (start_type == HTYPE_NORMAL)
    {
        int h, m, s;
        Utils::from_string(shour, h);
        Utils::from_string(smin, m);
        Utils::from_string(ssec, s);
        v = h * 3600 + m * 60 + s;
    }
    else if (start_type == HTYPE_SUNRISE ||
             start_type == HTYPE_SUNSET ||
             start_type == HTYPE_NOON)
    {
        int rise_hour, rise_min, set_hour, set_min;
        computeSunSetRise(year, month, day,
                          rise_hour, rise_min,
                          set_hour, set_min);

        if (start_type == HTYPE_SUNRISE)
            v = rise_hour * 3600 + rise_min * 60;
        else if (start_type == HTYPE_SUNSET)
            v = set_hour * 3600 + set_min * 60;
        else if (start_type == HTYPE_NOON)
            v = ((rise_hour * 3600 + rise_min * 60) + (set_hour * 3600 + set_min * 60)) / 2.0;

        if (shour != "0" || smin != "0" || ssec != "0")
        {
            //there is an offset
            int h, m, s;
            Utils::from_string(shour, h);
            Utils::from_string(smin, m);
            Utils::from_string(ssec, s);
            v = v + start_offset * (h * 3600 + m * 60 + s);
        }
    }

    return v;
}

long TimeRange::getEndTimeSec(int year, int month, int day)
{
    long v = 0;

    if (end_type == HTYPE_NORMAL)
    {
        int h, m, s;
        Utils::from_string(ehour, h);
        Utils::from_string(emin, m);
        Utils::from_string(esec, s);
        v = h * 3600 + m * 60 + s;
    }
    else if (end_type == HTYPE_SUNRISE ||
             end_type == HTYPE_SUNSET ||
             end_type == HTYPE_NOON)
    {
        int rise_hour, rise_min, set_hour, set_min;
        computeSunSetRise(year, month, day,
                          rise_hour, rise_min,
                          set_hour, set_min);

        if (end_type == HTYPE_SUNRISE)
            v = rise_hour * 3600 + rise_min * 60;
        else if (end_type == HTYPE_SUNSET)
            v = set_hour * 3600 + set_min * 60;
        else if (end_type == HTYPE_NOON)
            v = ((rise_hour * 3600 + rise_min * 60) + (set_hour * 3600 + set_min * 60)) / 2.0;

        if (ehour != "0" || emin != "0" || esec != "0")
        {
            //there is an offset
            int h, m, s;
            Utils::from_string(ehour, h);
            Utils::from_string(emin, m);
            Utils::from_string(esec, s);
            v = v + end_offset * (h * 3600 + m * 60 + s);
        }
    }

    return v;
}

long TimeRange::getTimezoneOffset()
{
    /* The GMT offset calculation code below has been borrowed from the APR library
         * Licensed to the Apache Software Foundation (ASF) under one or more
         * contributor license agreements.  See the NOTICE file distributed with
         * this work for additional information regarding copyright ownership.
         * The ASF licenses this file to You under the Apache License, Version 2.0
         * (the "License"); you may not use this file except in compliance with
         * the License.  You may obtain a copy of the License at
         *
         *     http://www.apache.org/licenses/LICENSE-2.0
         *
         * Unless required by applicable law or agreed to in writing, software
         * distributed under the License is distributed on an "AS IS" BASIS,
         * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
         * See the License for the specific language governing permissions and
         * limitations under the License.
         */

    long off;
#if defined(HAVE_TM_GMTOFF) || defined(HAVE___TM_GMTOFF)
    off = 0;
#else
    struct timeval now;
    time_t t1, t2;
    struct tm t;

    gettimeofday(&now, NULL);
    t1 = now.tv_sec;
    t2 = 0;

    t = *gmtime(&t1);
    t.tm_isdst = 0; /* we know this GMT time isn't daylight-savings */
    t2 = mktime(&t);
    off = (long)difftime(t1, t2);
#endif

    tzset(); //Force reload of timezone data
    t1 = time(NULL);
    struct tm *timeinfo = localtime(&t1);

#if defined(HAVE_TM_GMTOFF)
    return timeinfo->tm_gmtoff;
#elif defined(HAVE___TM_GMTOFF)
    return timeinfo->__tm_gmtoff;
#else
    return off + (timeinfo->tm_isdst ? 3600 : 0);
#endif
}

void TimeRange::computeSunSetRise(int year, int month, int day,
                                  int &rise_hour, int &rise_min,
                                  int &set_hour, int &set_min)
{
    if (year == cyear && month == cmonth && day == cday &&
        (sunrise_hour_cache != 0 || sunrise_min_cache != 0 ||
         sunset_hour_cache != 0 || sunset_min_cache != 0))
    {
        rise_hour = sunrise_hour_cache;
        rise_min = sunrise_min_cache;
        set_hour = sunset_hour_cache;
        set_min = sunset_min_cache;

        return;
    }

    double longitude;
    double latitude;
    Params opt;

    Utils::get_config_options(opt);
    if (!opt.Exists("longitude") || !opt.Exists("latitude"))
    {
        longitude = 2.548828;
        latitude = 46.422713;

        cError() <<  "Horaire: To use sunset/sunrise, you have to set your longitude/latitude in configuration!";
        cError() <<  "Horaire: Please go to the webpage of the server to set these parameters.";
    }
    else
    {
        Utils::from_string(Utils::get_config_option("longitude"), longitude);
        Utils::from_string(Utils::get_config_option("latitude"), latitude);
    }

    double rise, set;
    int res;

    cInfo() <<  "Horaire: Computing sunrise/sunset for date " <<
                day << "/" << month << "/" << year;
    res = sun_rise_set(year, month, day, longitude, latitude, &rise, &set);

    if (res != 0)
    {
        rise_hour = 0;
        rise_min = 0;
        set_hour = 0;
        set_min = 0;

        cError() <<  "Horaire: Error in sunset/sunrise calculation!";

        return;
    }

    long tzOffset = getTimezoneOffset();
    rise_min = minutes(rise + minutes((double)tzOffset / 3600.0));
    rise_hour = hours(rise + (double)tzOffset / 3600.0);
    set_min = minutes(set + minutes((double)tzOffset / 3600.0));
    set_hour = hours(set + (double)tzOffset / 3600.0);

    std::stringstream streamrise, streamset;
    streamrise << std::setfill('0') << std::setw(2) << rise_hour << ":" << rise_min;
    streamset << std::setfill('0') << std::setw(2) << set_hour << ":" << set_min;
    cInfo() <<  "Horaire: sunrise is at " << streamrise.str() << " and sunset is at " <<
                streamset.str();

    sunrise_hour_cache = rise_hour;
    sunrise_min_cache = rise_min;
    sunset_hour_cache = set_hour;
    sunset_min_cache = set_min;
    cyear = year;
    cmonth = month;
    cday = day;
}

long TimeRange::getStartTimeSec()
{
    struct tm *ctime = NULL;
    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    ctime = localtime(&t);

    return getStartTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
}

long TimeRange::getEndTimeSec()
{
    struct tm *ctime = NULL;
    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    ctime = localtime(&t);

    return getEndTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
}

bool TimeRange::isSameStartEnd()
{
    struct tm *ctime = NULL;
    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    ctime = localtime(&t);

    long start = getStartTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
    long end = getEndTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);

    return start == end;
}

std::string TimeRange::toProtoCommand(int day) const
{
    std::string s = Utils::to_string(day + 1) + ":";
    s += shour + ":" + smin + ":" + ssec;
    s += ":" + Utils::to_string(start_type);
    s += ":" + Utils::to_string(start_offset); //1 or -1
    s += ":" + ehour + ":" + emin + ":" + esec;
    s += ":" + Utils::to_string(end_type);
    s += ":" + Utils::to_string(end_offset); //1 or -1

    return s;
}

Params TimeRange::toParams(int day) const
{
    Params p;

    p.Add("day", Utils::to_string(day + 1));
    p.Add("start_hour", shour);
    p.Add("start_min", smin);
    p.Add("start_sec", ssec);
    p.Add("start_type", Utils::to_string(start_type));
    p.Add("start_offset", Utils::to_string(start_offset));

    p.Add("end_hour", ehour);
    p.Add("end_min", emin);
    p.Add("end_sec", esec);
    p.Add("end_type", Utils::to_string(end_type));
    p.Add("end_offset", Utils::to_string(end_offset));

    return p;
}

std::string TimeRange::toString()
{
    std::stringstream str;

    struct tm *ctime = NULL;
    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    ctime = localtime(&t);

    long start = getStartTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);
    long end = getEndTimeSec(ctime->tm_year + 1900, ctime->tm_mon + 1, ctime->tm_mday);

    if (start_type == HTYPE_NORMAL)
    {
        str << "[Start: normal] (" << Utils::time2string_digit(start) << ") ===> ";
    }
    else
    {
        if (start_type == HTYPE_SUNRISE) str << "[Start: sunrise] ";
        if (start_type == HTYPE_SUNSET) str << "[Start: sunset] ";
        if (start_type == HTYPE_NOON) str << "[Start: noon] ";

        str << "(" << Utils::time2string_digit(start) << ") ";

        if (shour != "0" || smin != "0" || ssec != "0")
        {
            if (start_offset > 0)
                str << " +offset ";
            else
                str << " -offset ";
            str << "[" << shour << ":" << smin << ":" << ssec << "] ===> ";
        }
        else
            str << " ===> ";
    }

    if (end_type == HTYPE_NORMAL)
    {
        str << "[End: normal] (" << Utils::time2string_digit(end) << ")";
    }
    else
    {
        if (end_type == HTYPE_SUNRISE) str << "[End: sunrise] ";
        if (end_type == HTYPE_SUNSET) str << "[End: sunset] ";
        if (end_type == HTYPE_NOON) str << "[End: noon] ";

        str << "(" << Utils::time2string_digit(end) << ") ";

        if (ehour != "0" || emin != "0" || esec != "0")
        {
            if (end_offset > 0)
                str << " +offset ";
            else
                str << " -offset ";
            str << "[" << ehour << ":" << emin << ":" << esec << "]";
        }
    }

    return str.str();
}
