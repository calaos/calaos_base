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
#ifndef S_InPlageHoraire_H
#define S_InPlageHoraire_H

#include <Input.h>
#include <time.h>
#include <TimeRange.h>

namespace Calaos
{

class InPlageHoraire : public Input
{
        protected:
                bool value;

                vector<TimeRange> plg_lundi;
                vector<TimeRange> plg_mardi;
                vector<TimeRange> plg_mercredi;
                vector<TimeRange> plg_jeudi;
                vector<TimeRange> plg_vendredi;
                vector<TimeRange> plg_samedi;
                vector<TimeRange> plg_dimanche;

                void LoadPlage(TiXmlElement *node, vector<TimeRange> &plage);
                void SavePlage(TiXmlElement *node, string day, vector<TimeRange> &plage);

        public:
                InPlageHoraire(Params &p);
                ~InPlageHoraire();

                virtual DATA_TYPE get_type() { return TBOOL; }
                virtual bool get_value_bool() { return value; }

                //Here we store months when plagehoraire is active
                enum { JANUARY = 0, FEBRUARY, MARCH, APRIL, MAY, JUNE, JULY, AUGUST, SEPTEMBER, OCTOBER, NOVEMBER, DECEMBER };
                bitset<12> months;

                void AddLundi(TimeRange &horaire) { plg_lundi.push_back(horaire); }
                void AddMardi(TimeRange &horaire) { plg_mardi.push_back(horaire); }
                void AddMercredi(TimeRange &horaire) { plg_mercredi.push_back(horaire); }
                void AddJeudi(TimeRange &horaire) { plg_jeudi.push_back(horaire); }
                void AddVendredi(TimeRange &horaire) { plg_vendredi.push_back(horaire); }
                void AddSamedi(TimeRange &horaire) { plg_samedi.push_back(horaire); }
                void AddDimanche(TimeRange &horaire) { plg_dimanche.push_back(horaire); }

                vector<TimeRange> &getLundi() { return plg_lundi; }
                vector<TimeRange> &getMardi() { return plg_mardi; }
                vector<TimeRange> &getMercredi() { return plg_mercredi; }
                vector<TimeRange> &getJeudi() { return plg_jeudi; }
                vector<TimeRange> &getVendredi() { return plg_vendredi; }
                vector<TimeRange> &getSamedi() { return plg_samedi; }
                vector<TimeRange> &getDimanche() { return plg_dimanche; }

                void setLundi(vector<TimeRange> &h) { plg_lundi = h; }
                void setMardi(vector<TimeRange> &h) { plg_mardi = h; }
                void setMercredi(vector<TimeRange> &h) { plg_mercredi = h; }
                void setJeudi(vector<TimeRange> &h) { plg_jeudi = h; }
                void setVendredi(vector<TimeRange> &h) { plg_vendredi = h; }
                void setSamedi(vector<TimeRange> &h) { plg_samedi = h; }
                void setDimanche(vector<TimeRange> &h) { plg_dimanche = h; }

                void clear();

                virtual void hasChanged();

                virtual bool LoadFromXml(TiXmlElement *node);
                virtual bool SaveToXml(TiXmlElement *node);
};

}
#endif
