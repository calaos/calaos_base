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
#include "InPlageHoraire.h"
#include "ListeRule.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(InPlageHoraire)
REGISTER_IO_USERTYPE(TimeRange, InPlageHoraire)

InPlageHoraire::InPlageHoraire(Params &p):
    IOBase(p, IOBase::IO_INPUT),
    value(false)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("TimeRange");
    ioDoc->aliasAdd("InPlageHoraire");
    ioDoc->descriptionSet(_("Represent a time range object. A time range is true if current time is in one of the included range, false otherwise. The time range also support weekdays and months. "
                            "A range whose end is before its start wraps over midnight: it starts on the weekday it is attached to and ends the next morning. "
                            "For example a range 23:00 -> 01:00 set on monday is true from monday 23:00 to tuesday 01:00, and never on monday between 00:00 and 01:00. "
                            "Beware that bounds relative to sunrise/sunset move with the season: a range like sunset -> 23:00 is a normal range most of the year but starts to wrap over midnight when sunset gets later than 23:00. "
                            "Such a range is reported in the log the first time it is evaluated as wrapping."));
    ioDoc->paramAdd("visible", _("A time range can't be visible. Always false."), IODoc::TYPE_BOOL, false, "false", true);

    ioDoc->conditionAdd("true", _("Event triggered when entering the range"));
    ioDoc->conditionAdd("false", _("Event triggered when exiting the range"));
    ioDoc->conditionAdd("changed", _("Event on any change of range"));

    ListeRule::Instance().Add(this); //add this specific input to the EventLoop
    cDebugDom("input") << get_param("id") << ": Ok";

    set_param("visible", "false");
    set_param("gui_type", "time_range");

    months.set(); //set all months by default
}

InPlageHoraire::~InPlageHoraire()
{
}

void InPlageHoraire::clear()
{
    plg_monday.clear();
    plg_tuesday.clear();
    plg_wednesday.clear();
    plg_thursday.clear();
    plg_friday.clear();
    plg_saturday.clear();
    plg_sunday.clear();
}

static const char *weekdayName(int wday)
{
    switch (wday)
    {
    case TimeRange::MONDAY: return "monday";
    case TimeRange::TUESDAY: return "tuesday";
    case TimeRange::WEDNESDAY: return "wednesday";
    case TimeRange::THURSDAY: return "thursday";
    case TimeRange::FRIDAY: return "friday";
    case TimeRange::SATURDAY: return "saturday";
    case TimeRange::SUNDAY: return "sunday";
    default: return "unknown day";
    }
}

vector<TimeRange> *InPlageHoraire::getRangesForWeekday(int wday)
{
    switch (wday)
    {
    case TimeRange::MONDAY: return &plg_monday;
    case TimeRange::TUESDAY: return &plg_tuesday;
    case TimeRange::WEDNESDAY: return &plg_wednesday;
    case TimeRange::THURSDAY: return &plg_thursday;
    case TimeRange::FRIDAY: return &plg_friday;
    case TimeRange::SATURDAY: return &plg_saturday;
    case TimeRange::SUNDAY: return &plg_sunday;
    default: return NULL;
    }
}

struct tm InPlageHoraire::previousDay(const struct tm &day)
{
    struct tm prev = day;

    prev.tm_mday--;
    prev.tm_hour = 12; //noon, no DST transition ever lands there
    prev.tm_min = 0;
    prev.tm_sec = 0;
    prev.tm_isdst = -1; //let mktime() find out the DST flag of that day

    //mktime() normalizes the whole structure: month/year rollover, tm_wday...
    if (mktime(&prev) == (time_t) -1)
    {
        cErrorDom("input") << "Failed to compute the day before "
                           << day.tm_mday << "/" << day.tm_mon + 1 << "/" << day.tm_year + 1900;

        prev = day;
        prev.tm_wday = TimeRange::BADDAY; //no schedule is attached to it
    }

    return prev;
}

/* Is `cur` (a second of day of the *current* day) inside the ranges scheduled
 * for the day described by `date`?
 *
 * A range whose end is before its start wraps over midnight: it is the natural
 * way of writing an overnight period (23:00 -> 01:00). Such a range belongs to
 * the day it is attached to and runs until the next morning: "monday
 * 23:00 -> 01:00" means monday 23:00 up to tuesday 01:00, it does NOT also
 * match monday between 00:00 and 01:00.
 *
 * That is why it is evaluated twice: once with previousDay == false, where it
 * covers [start, end of day] of its own day, and once the next day with
 * previousDay == true, where it covers [start of day, end].
 */
bool InPlageHoraire::isInRanges(vector<TimeRange> *plage, long cur,
                                const struct tm &date, bool previousDay)
{
    if (!plage) return false;

    bool val = false;

    //no early exit: every range is evaluated, so that a wrapping one is
    //reported in the log even when an earlier range already matched
    for (uint i = 0;i < plage->size();i++)
    {
        TimeRange &h = (*plage)[i];

        //a range with an unparsable bound is not evaluated at all, its bounds
        //would silently fall back to 00:00:00
        if (!h.isValid())
            continue;

        long start_time = h.getStartTimeSec(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
        long end_time = h.getEndTimeSec(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);

        if (start_time <= end_time)
        {
            //a plain range lives entirely inside its own day
            if (!previousDay && cur >= start_time && cur <= end_time)
                val = true;
        }
        else
        {
            //name the schedule and the weekday the range is attached to, the
            //bounds alone do not identify it
            h.logWrapOnce(get_param("id") + " (" + weekdayName(date.tm_wday) + ")",
                          start_time, end_time);

            if (previousDay)
            {
                //tail of yesterday's overnight range, [00:00, end]
                if (cur <= end_time)
                    val = true;
            }
            else
            {
                //head of today's overnight range, [start, end of day]
                if (cur >= start_time)
                    val = true;
            }
        }
    }

    return val;
}

void InPlageHoraire::hasChanged()
{
    if (!isEnabled()) return;

    bool val = false;

    tzset(); //Force reload of timezone data
    time_t t = time(NULL);
    //copy, localtime() returns a pointer to a shared buffer
    struct tm today = *localtime(&t);

    //If the month is not set, the time_range is always false
    if (months.test(today.tm_mon))
    {
        long cur = today.tm_hour * 3600 +
                   today.tm_min * 60 +
                   today.tm_sec;

        //ranges of today, plus the ranges of yesterday that run over midnight
        struct tm yesterday = previousDay(today);

        //both are evaluated, no short circuit, so that a wrapping range of
        //yesterday is reported even when today already matched
        bool in_today = isInRanges(getRangesForWeekday(today.tm_wday), cur, today, false);
        bool in_yesterday = isInRanges(getRangesForWeekday(yesterday.tm_wday), cur, yesterday, true);

        val = in_today || in_yesterday;
    }

    if (val != value)
    {
        value = val;
        cInfoDom("input") << get_param("id") << ": Changed to " << (value?"true":"false");

        EmitSignalIO();

        EventManager::create(CalaosEvent::EventIOChanged,
                             { { "id", get_param("id") },
                               { "state", val?"true":"false" } });
    }
}

void InPlageHoraire::LoadRange(pugi::xml_node node, vector<TimeRange> &plage)
{
    //Element-only traversal on purpose: unlike every other loop of the port
    //this one reads the attributes of EVERY child without checking its name,
    //so a non-element child would be turned into an empty TimeRange.
    pugi::xml_node cnode = XmlUtils::firstChildElement(node);
    for(; cnode; cnode = XmlUtils::nextSiblingElement(cnode))
    {
        TimeRange h;
        if (cnode.attribute("start_type"))
        {
            from_string(string(cnode.attribute("start_type").as_string()), h.start_type);
            if (h.start_type < 0 || h.start_type > 3)
                h.start_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode.attribute("start_offset"))
        {
            //T3.25 (review): _or_keep. TimeRange::start_offset is 1 in-class and the
            //clamp below only maps <0 to -1 and >0 to 1 - it cannot repair a 0. A
            //blank start_offset attribute therefore ANNULLED the sunrise/sunset
            //offset instead of keeping its documented sign.
            from_string_or_keep(string(cnode.attribute("start_offset").as_string()), h.start_offset);
            if (h.start_offset < 0) h.start_offset = -1;
            if (h.start_offset > 0) h.start_offset = 1;
        }
        if (cnode.attribute("start_hour")) h.shour = cnode.attribute("start_hour").as_string();
        if (cnode.attribute("start_min")) h.smin = cnode.attribute("start_min").as_string();
        if (cnode.attribute("start_sec")) h.ssec = cnode.attribute("start_sec").as_string();

        if (cnode.attribute("end_type"))
        {
            from_string(string(cnode.attribute("end_type").as_string()), h.end_type);
            if (h.end_type < 0 || h.end_type > 3)
                h.end_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode.attribute("end_offset"))
        {
            //T3.25 (review): _or_keep, same reason as start_offset above.
            from_string_or_keep(string(cnode.attribute("end_offset").as_string()), h.end_offset);
            if (h.end_offset < 0) h.end_offset = -1;
            if (h.end_offset > 0) h.end_offset = 1;
        }
        if (cnode.attribute("end_hour")) h.ehour = cnode.attribute("end_hour").as_string();
        if (cnode.attribute("end_min")) h.emin = cnode.attribute("end_min").as_string();
        if (cnode.attribute("end_sec")) h.esec = cnode.attribute("end_sec").as_string();

        stringstream sstart, sstop;
        if (h.start_type == TimeRange::HTYPE_NORMAL)
        {
            sstart << h.shour << ":" << h.smin << ":" << h.ssec;
        }
        else if (h.start_type == TimeRange::HTYPE_SUNRISE)
        {
            sstart << " Sunrise";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }
        else if (h.start_type == TimeRange::HTYPE_SUNSET)
        {
            sstart << " Sunset";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }
        else if (h.start_type == TimeRange::HTYPE_NOON)
        {
            sstart << " Noon";
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                if (h.start_offset > 0)
                    sstart << " +offset ";
                else
                    sstart << " -offset ";
                sstart << h.shour << ":" << h.smin << ":" << h.ssec;
            }
        }

        if (h.end_type == TimeRange::HTYPE_NORMAL)
        {
            sstop << h.ehour << ":" << h.emin << ":" << h.esec;
        }
        else if (h.end_type == TimeRange::HTYPE_SUNRISE)
        {
            sstop << " Sunrise";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstart << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }
        else if (h.end_type == TimeRange::HTYPE_SUNSET)
        {
            sstop << " Sunset";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstop << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }
        else if (h.end_type == TimeRange::HTYPE_NOON)
        {
            sstop << " Noon";
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                if (h.end_offset > 0)
                    sstop << " +offset ";
                else
                    sstop << " -offset ";
                sstop << h.ehour << ":" << h.emin << ":" << h.esec;
            }
        }

        cDebugDom("input") << "InPlageHoraire::LoadPlage(): Adding plage: "
                           << sstart.str() << " ===> " << sstop.str();

        plage.push_back(h);
    }
}

bool InPlageHoraire::LoadFromXml(pugi::xml_node pnode)
{
    pugi::xml_node node = XmlUtils::firstChildElement(pnode);

    cDebugDom("input") << "InPlageHoraire::LoadFromXml(): Loading plage content";

    //try to load months
    if (pnode.attribute("months"))
    {
        string m = pnode.attribute("months").as_string();
        //reverse to have a left to right months representation
        std::reverse(m.begin(), m.end());

        try
        {
            bitset<12> mset(m);
            months = mset;
        }
        catch(...)
        {
            cErrorDom("input") << "Wrong parameters for months: " << m;
            cErrorDom("input") << "Setting all months to active";

            months.set(); //set all months by default
        }
    }

    for(; node; node = XmlUtils::nextSiblingElement(node))
    {
        const string nodeName = node.name();
        if (nodeName == "calaos:lundi")
            LoadRange(node, plg_monday);
        else if (nodeName == "calaos:mardi")
            LoadRange(node, plg_tuesday);
        else if (nodeName == "calaos:mercredi")
            LoadRange(node, plg_wednesday);
        else if (nodeName == "calaos:jeudi")
            LoadRange(node, plg_thursday);
        else if (nodeName == "calaos:vendredi")
            LoadRange(node, plg_friday);
        else if (nodeName == "calaos:samedi")
            LoadRange(node, plg_saturday);
        else if (nodeName == "calaos:dimanche")
            LoadRange(node, plg_sunday);
    }

    return true;
}

void InPlageHoraire::SaveRange(pugi::xml_node node, string day, vector<TimeRange> &plage)
{
    if (plage.size() <= 0) return; //don't create node if empty

    pugi::xml_node day_node = node.append_child((string("calaos:") + day).c_str());

    for (uint i = 0;i < plage.size();i++)
    {
        pugi::xml_node period_node = day_node.append_child("calaos:plage");

        TimeRange &h = plage[i];

        XmlUtils::setAttribute(period_node, "start_type", Utils::to_string(h.start_type));
        if (h.start_type == TimeRange::HTYPE_NORMAL)
        {
            XmlUtils::setAttribute(period_node, "start_hour", h.shour);
            XmlUtils::setAttribute(period_node, "start_min", h.smin);
            XmlUtils::setAttribute(period_node, "start_sec", h.ssec);
        }
        else if (h.start_type == TimeRange::HTYPE_SUNRISE ||
                 h.start_type == TimeRange::HTYPE_SUNSET ||
                 h.start_type == TimeRange::HTYPE_NOON)
        {
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                XmlUtils::setAttribute(period_node, "start_hour", h.shour);
                XmlUtils::setAttribute(period_node, "start_min", h.smin);
                XmlUtils::setAttribute(period_node, "start_sec", h.ssec);
                XmlUtils::setAttribute(period_node, "start_offset", h.start_offset);
            }
        }

        XmlUtils::setAttribute(period_node, "end_type", Utils::to_string(h.end_type));
        if (h.end_type == TimeRange::HTYPE_NORMAL)
        {
            XmlUtils::setAttribute(period_node, "end_hour", h.ehour);
            XmlUtils::setAttribute(period_node, "end_min", h.emin);
            XmlUtils::setAttribute(period_node, "end_sec", h.esec);
        }
        else if (h.end_type == TimeRange::HTYPE_SUNRISE ||
                 h.end_type == TimeRange::HTYPE_SUNSET ||
                 h.end_type == TimeRange::HTYPE_NOON)
        {
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                XmlUtils::setAttribute(period_node, "end_hour", h.ehour);
                XmlUtils::setAttribute(period_node, "end_min", h.emin);
                XmlUtils::setAttribute(period_node, "end_sec", h.esec);
                XmlUtils::setAttribute(period_node, "end_offset", h.end_offset);
            }
        }
    }
}

bool InPlageHoraire::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cnode = node.append_child("calaos:input");

    for (int i = 0;i < get_params().size();i++)
    {
        string key, val;
        get_params().get_item(i, key, val);
        XmlUtils::setAttribute(cnode, key, val);
    }

    //Save months
    stringstream ssmonth;
    ssmonth << months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    //setAttribute(), not append: "months" is already in the parameter map
    //(IOFactory::readParams() puts every attribute of the node there at load
    //time), so appending would emit it twice.
    XmlUtils::setAttribute(cnode, "months", str);

    SaveRange(cnode, "lundi", plg_monday);
    SaveRange(cnode, "mardi", plg_tuesday);
    SaveRange(cnode, "mercredi", plg_wednesday);
    SaveRange(cnode, "jeudi", plg_thursday);
    SaveRange(cnode, "vendredi", plg_friday);
    SaveRange(cnode, "samedi", plg_saturday);
    SaveRange(cnode, "dimanche", plg_sunday);

    return true;
}
