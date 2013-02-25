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
#ifndef  CALENDAR_INC
#define  CALENDAR_INC

#include <Utils.h>
#include <Ecore.h>
#include <Ecore_File.h>

#define TMP_CURRENT_ZONE    "/tmp/calaos_timezone"
#define CURRENT_ZONE    "/etc/timezone"
#define LOCALTIME       "/etc/localtime"
#define ZONEPATH        "/usr/share/zoneinfo/"

class TimeZoneElt
{
        public:
                /* la cl du pays  mettre dans /etc/timezone */
                string key;
                /* le chemin complet du pays (continent / pays / ville) */
                string country;
                /* le dcalage horaire sous forme de chaine de caractre */
                string decalageStr;
                /* le dcalage horaire sous forme d'entier (non utilis) */
                int decalage;
                /* le code du pays (fr, de ...) */
                string code;

                TimeZoneElt(string k, string c, string dstr, int d, string id);
};

class TimeZone
{
        public:
                /* le fuseau horaire selectionn,  modifier directement
                 *avec l'indice du fuseaux horaire dans "timeZone" */
                int current;

                /* la liste des fuseaux horaires */
                vector<TimeZoneElt> timeZone;

                TimeZone();
                int loadCurrentTimeZone();
};

class Calendar
{
        private:
                bool restart;
        public:

                Ecore_Exe *exe;

                static const int mounths[13];
                static const char *days[7];
                static const char* M[13];

                int hours;
                int minutes;
                int secondes;

                /* A vrai pour utiliser ntp */
                bool ntp;

                int year;
                int month;
                int day;

                /* position du jour dans une semaine (lundi:1 ....) */
                int dayId;

                TimeZone timeZone;

                Ecore_Event_Handler* handler;
                Ecore_Event_Handler* handler2;

                Calendar();

                void setRestart(bool r);
                bool isRestart();

                void initClock();
                void hoursDec();
                void hoursUp();
                void setHours(int val);
                void minutesDec();
                void minutesUp();
                void setMinutes(int val);
                void secondesDec();
                void secondesUp();
                void setSecondes(int val);
                void setNtp(bool b);
                void timezoneSelect(int id);

                string hoursToString();
                string minutesToString();
                string secondesToString();

                void monthUp();
                void monthDown();
                void setMonth(int m);

                void dayUp();
                void dayDown();
                void setDay(int m);

                void yearUp();
                void yearDown();
                void setYear(int m);

                const string getDayFromDate();
                int getDayIdFromDate();
                void initDate();
                const string getMonthFromDate();
                int getNbDaysInMonth();

                void apply();
                void applyTimezone();

        private:
                bool isleap(int n);
                void updateDay();
};

#endif
