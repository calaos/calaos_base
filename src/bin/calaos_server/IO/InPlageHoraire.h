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
#ifndef S_InPlageHoraire_H
#define S_InPlageHoraire_H

#include "IOBase.h"
#include <time.h>
#include "TimeRange.h"

namespace Calaos
{

class InPlageHoraire : public IOBase
{
protected:
    bool value;

    vector<TimeRange> plg_monday;
    vector<TimeRange> plg_tuesday;
    vector<TimeRange> plg_wednesday;
    vector<TimeRange> plg_thursday;
    vector<TimeRange> plg_friday;
    vector<TimeRange> plg_saturday;
    vector<TimeRange> plg_sunday;

    void LoadRange(TiXmlElement *node, vector<TimeRange> &plage);
    void SaveRange(TiXmlElement *node, string day, vector<TimeRange> &plage);

public:
    InPlageHoraire(Params &p);
    ~InPlageHoraire();

    virtual DATA_TYPE get_type() { return TBOOL; }
    virtual bool get_value_bool() { return value; }

    //Here we store months when plagehoraire is active
    enum { JANUARY = 0, FEBRUARY, MARCH, APRIL, MAY, JUNE, JULY, AUGUST, SEPTEMBER, OCTOBER, NOVEMBER, DECEMBER };
    bitset<12> months;

    void AddMonday(TimeRange &horaire) { plg_monday.push_back(horaire); }
    void AddTuesday(TimeRange &horaire) { plg_tuesday.push_back(horaire); }
    void AddWednesday(TimeRange &horaire) { plg_wednesday.push_back(horaire); }
    void AddThursday(TimeRange &horaire) { plg_thursday.push_back(horaire); }
    void AddFriday(TimeRange &horaire) { plg_friday.push_back(horaire); }
    void AddSaturday(TimeRange &horaire) { plg_saturday.push_back(horaire); }
    void AddSunday(TimeRange &horaire) { plg_sunday.push_back(horaire); }

    vector<TimeRange> &getMonday() { return plg_monday; }
    vector<TimeRange> &getTuesday() { return plg_tuesday; }
    vector<TimeRange> &getWednesday() { return plg_wednesday; }
    vector<TimeRange> &getThursday() { return plg_thursday; }
    vector<TimeRange> &getFriday() { return plg_friday; }
    vector<TimeRange> &getSaturday() { return plg_saturday; }
    vector<TimeRange> &getSunday() { return plg_sunday; }

    void setMonday(vector<TimeRange> &h) { plg_monday = h; }
    void setTuesday(vector<TimeRange> &h) { plg_tuesday = h; }
    void setWednesday(vector<TimeRange> &h) { plg_wednesday = h; }
    void setThursday(vector<TimeRange> &h) { plg_thursday = h; }
    void setFriday(vector<TimeRange> &h) { plg_friday = h; }
    void setSaturday(vector<TimeRange> &h) { plg_saturday = h; }
    void setSunday(vector<TimeRange> &h) { plg_sunday = h; }

    void clear();

    virtual void hasChanged();

    virtual bool LoadFromXml(TiXmlElement *node);
    virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
