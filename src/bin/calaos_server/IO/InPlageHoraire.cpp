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
                            "For example a range 23:00 -> 01:00 set on monday is true from monday 23:00 to tuesday 01:00, and never on monday between 00:00 and 01:00."));
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
                return true;
        }
        else if (previousDay)
        {
            //tail of yesterday's overnight range, [00:00, end]
            if (cur <= end_time)
                return true;
        }
        else
        {
            //head of today's overnight range, [start, end of day]
            if (cur >= start_time)
                return true;
        }
    }

    return false;
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
        time_t t_yesterday = t - 24 * 3600;
        struct tm yesterday = *localtime(&t_yesterday);

        val = isInRanges(getRangesForWeekday(today.tm_wday), cur, today, false) ||
              isInRanges(getRangesForWeekday(yesterday.tm_wday), cur, yesterday, true);
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

void InPlageHoraire::LoadRange(TiXmlElement *node, vector<TimeRange> &plage)
{
    TiXmlHandle docHandle(node);
    TiXmlElement *cnode = docHandle.FirstChildElement().ToElement();
    for(; cnode; cnode = cnode->NextSiblingElement())
    {
        TimeRange h;
        if (cnode->Attribute("start_type"))
        {
            from_string(string(cnode->Attribute("start_type")), h.start_type);
            if (h.start_type < 0 || h.start_type > 3)
                h.start_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode->Attribute("start_offset"))
        {
            from_string(string(cnode->Attribute("start_offset")), h.start_offset);
            if (h.start_offset < 0) h.start_offset = -1;
            if (h.start_offset > 0) h.start_offset = 1;
        }
        if (cnode->Attribute("start_hour")) h.shour = cnode->Attribute("start_hour");
        if (cnode->Attribute("start_min")) h.smin = cnode->Attribute("start_min");
        if (cnode->Attribute("start_sec")) h.ssec = cnode->Attribute("start_sec");

        if (cnode->Attribute("end_type"))
        {
            from_string(string(cnode->Attribute("end_type")), h.end_type);
            if (h.end_type < 0 || h.end_type > 3)
                h.end_type = TimeRange::HTYPE_NORMAL;
        }
        if (cnode->Attribute("end_offset"))
        {
            from_string(string(cnode->Attribute("end_offset")), h.end_offset);
            if (h.end_offset < 0) h.end_offset = -1;
            if (h.end_offset > 0) h.end_offset = 1;
        }
        if (cnode->Attribute("end_hour")) h.ehour = cnode->Attribute("end_hour");
        if (cnode->Attribute("end_min")) h.emin = cnode->Attribute("end_min");
        if (cnode->Attribute("end_sec")) h.esec = cnode->Attribute("end_sec");

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

bool InPlageHoraire::LoadFromXml(TiXmlElement *pnode)
{
    TiXmlHandle docHandle(pnode);
    TiXmlElement *node = docHandle.FirstChildElement().ToElement();

    cDebugDom("input") << "InPlageHoraire::LoadFromXml(): Loading plage content";

    //try to load months
    if (pnode->Attribute("months"))
    {
        string m = pnode->Attribute("months");
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

    for(; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:lundi")
            LoadRange(node, plg_monday);
        else if (node->ValueStr() == "calaos:mardi")
            LoadRange(node, plg_tuesday);
        else if (node->ValueStr() == "calaos:mercredi")
            LoadRange(node, plg_wednesday);
        else if (node->ValueStr() == "calaos:jeudi")
            LoadRange(node, plg_thursday);
        else if (node->ValueStr() == "calaos:vendredi")
            LoadRange(node, plg_friday);
        else if (node->ValueStr() == "calaos:samedi")
            LoadRange(node, plg_saturday);
        else if (node->ValueStr() == "calaos:dimanche")
            LoadRange(node, plg_sunday);
    }

    return true;
}

void InPlageHoraire::SaveRange(TiXmlElement *node, string day, vector<TimeRange> &plage)
{
    if (plage.size() <= 0) return; //don't create node if empty

    TiXmlElement *day_node = new TiXmlElement(string("calaos:") + day);
    node->LinkEndChild(day_node);

    for (uint i = 0;i < plage.size();i++)
    {
        TiXmlElement *period_node = new TiXmlElement("calaos:plage");
        day_node->LinkEndChild(period_node);

        TimeRange &h = plage[i];

        period_node->SetAttribute("start_type", Utils::to_string(h.start_type));
        if (h.start_type == TimeRange::HTYPE_NORMAL)
        {
            period_node->SetAttribute("start_hour", h.shour);
            period_node->SetAttribute("start_min", h.smin);
            period_node->SetAttribute("start_sec", h.ssec);
        }
        else if (h.start_type == TimeRange::HTYPE_SUNRISE ||
                 h.start_type == TimeRange::HTYPE_SUNSET ||
                 h.start_type == TimeRange::HTYPE_NOON)
        {
            if (h.shour != "0" || h.smin != "0" || h.ssec != "0")
            {
                period_node->SetAttribute("start_hour", h.shour);
                period_node->SetAttribute("start_min", h.smin);
                period_node->SetAttribute("start_sec", h.ssec);
                period_node->SetAttribute("start_offset", h.start_offset);
            }
        }

        period_node->SetAttribute("end_type", Utils::to_string(h.end_type));
        if (h.end_type == TimeRange::HTYPE_NORMAL)
        {
            period_node->SetAttribute("end_hour", h.ehour);
            period_node->SetAttribute("end_min", h.emin);
            period_node->SetAttribute("end_sec", h.esec);
        }
        else if (h.end_type == TimeRange::HTYPE_SUNRISE ||
                 h.end_type == TimeRange::HTYPE_SUNSET ||
                 h.end_type == TimeRange::HTYPE_NOON)
        {
            if (h.ehour != "0" || h.emin != "0" || h.esec != "0")
            {
                period_node->SetAttribute("end_hour", h.ehour);
                period_node->SetAttribute("end_min", h.emin);
                period_node->SetAttribute("end_sec", h.esec);
                period_node->SetAttribute("end_offset", h.end_offset);
            }
        }
    }
}

bool InPlageHoraire::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:input");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, val;
        get_params().get_item(i, key, val);
        cnode->SetAttribute(key, val);
    }

    //Save months
    stringstream ssmonth;
    ssmonth << months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    cnode->SetAttribute("months", str);

    SaveRange(cnode, "lundi", plg_monday);
    SaveRange(cnode, "mardi", plg_tuesday);
    SaveRange(cnode, "mercredi", plg_wednesday);
    SaveRange(cnode, "jeudi", plg_thursday);
    SaveRange(cnode, "vendredi", plg_friday);
    SaveRange(cnode, "samedi", plg_saturday);
    SaveRange(cnode, "dimanche", plg_sunday);

    return true;
}
