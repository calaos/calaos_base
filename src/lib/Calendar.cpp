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
#include <Calendar.h>

Eina_Bool _CalendarHandle1(void *data, int type, void *event);
Eina_Bool _CalendarHandle2(void *data, int type, void *event);

const int Calendar::mounths[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
const char *Calendar::days[] = { "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi", "Dimanche" };
const char *Calendar::M[] =
        { "", "Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Aout", "Septembre", "Octobre",
          "Novembre", "Decembre" };

TimeZone::TimeZone()
{
        timeZone.push_back(TimeZoneElt("Europe/London", "Europe / Royaume Unis / Londre", "GMT+00:00", 0, "gb"));
        timeZone.push_back(TimeZoneElt("Europe/Lisbon", "Europe / Portugal / Lisbonne", "GMT+00:00", 0, "pt"));
        timeZone.push_back(TimeZoneElt("Europe/Paris", "Europe / France / Paris", "GMT+01:00", 1, "fr"));
        timeZone.push_back(TimeZoneElt("Europe/Zurich", "Europe / Suisse / Zurich", "GMT+01:00", 1, "ch"));
        timeZone.push_back(TimeZoneElt("Europe/Berlin", "Europe / Allemagne / Berlin", "GMT+01:00", 1, "de"));
        timeZone.push_back(TimeZoneElt("Europe/Madrid", "Europe / Espagne / Madrid", "GMT+01:00", 1, "es"));
        timeZone.push_back(TimeZoneElt("Europe/Rome", "Europe / Italie / Rome", "GMT+01:00", 1, "it"));
        timeZone.push_back(TimeZoneElt("Europe/Helsinki", "Europe / Finland / Helsinki", "GMT+02:00", 2, "fi"));
        timeZone.push_back(TimeZoneElt("Asia/Moscow", "Asie / Russie / Moscou", "GMT+03:00", 3, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Samara", "Asie / Russie / Samara", "GMT+04:00", 4, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Yekaterinburg", "Asie / Russie / Yekaterinburg", "GMT+05:00", 5, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Kolkata", "Asie / Inde / Calcutta", "GMT+05:30", 5, "ru"));

        timeZone.push_back(TimeZoneElt("Asia/Omsk", "Asie / Russie / Omsk", "GMT+06:00", 6, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Krasnoyarsk", "Asie / Russie / Krasnoyarsk", "GMT+07:00", 7, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Shanghai", "Asie / Chine / Shanghai", "GMT+08:00", 8, "ch"));
        timeZone.push_back(TimeZoneElt("Asia/Tokyo", "Asie / Japon / Tokyo", "GMT+09:00", 9, "jp"));
        timeZone.push_back(TimeZoneElt("Asia/Vladivostok", "Asie / Russie / Vladivostok", "GMT+10:00", 10, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Magadan", "Asie / Russie / Magadan", "GMT+11:00", 11, "ru"));
        timeZone.push_back(TimeZoneElt("Asia/Anadyr", "Asie / Russie / Anadyr", "GMT+12:00", 12, "ru"));



        timeZone.push_back(TimeZoneElt("America/Los_Angeles", "Amérique / États Unis / Los Angeles", "GMT-08:00", -8, "us"));
        timeZone.push_back(TimeZoneElt("America/Denver", "Amérique / États Unis / Denver", "GMT-07:00", -7, "us"));
        timeZone.push_back(TimeZoneElt("America/Chicago", "Amérique / États Unis / Chicago", "GMT-06:00", -6, "us"));
        timeZone.push_back(TimeZoneElt("America/New_York", "Amérique / États Unis / New York", "GMT-05:00", -5, "us"));
        timeZone.push_back(TimeZoneElt("America/Halifax", "Amérique / Canada / Halifax", "GMT-04:00", -5, "ca"));
        timeZone.push_back(TimeZoneElt("America/Belem", "Amérique / Brézil / Belem", "GMT-03:00", -6, "bt"));

        loadCurrentTimeZone();
}

/*
 * Charge le fuseaux horaire actuel du système
 */
int TimeZone::loadCurrentTimeZone()
{
        std::ifstream fichier("/etc/timezone");

        if (fichier)
        {
                std::string ligne;
                while (std::getline(fichier, ligne) && ligne == "\n")
                        ;

                for (unsigned int i = 0; i < timeZone.size(); i++)
                        if (timeZone[i].key == ligne)
                        {
                                current = i;
                                return i;
                        }
        }
        current = -1;
        return -1;
}

TimeZoneElt::TimeZoneElt(string k, string c, string dstr, int d, string id)
{
        key = k;
        country = c;
        decalageStr = dstr;
        decalage = d;
        code = id;
}

Calendar::Calendar():exe(NULL), handler(NULL)
{
        initClock();
}

/*
 * Initialise le calendrier avec la date et l'heure actuelle du système
 */
void Calendar::initClock()
{
        //récupération de l'heure du système
        time_t tt = time(NULL);
        struct tm *t = localtime(&tt);

        setHours(t->tm_hour);
        setMinutes(t->tm_min);
        setSecondes(t->tm_sec);

        //récupération de la date
        initDate();
}

/*
 * Initialise le calendrier avec la date actuelle du système
 */
void Calendar::initDate()
{
        time_t tt = time(NULL);
        struct tm *t = localtime(&tt);

        year = t->tm_year + 1900;
        day = t->tm_mday;
        month = t->tm_mon + 1;
}

/*
 * Retourne le jour sous forme de string (lundi, mardi ...)
 */
const string Calendar::getDayFromDate()
{
        updateDay();
        if(dayId<0 || dayId>7)
                return days[0];
        return days[dayId];
}

/*
 * Retourne l'indice du jour dans la semaine (lundi:1, mardi:2 ....)
 */
int Calendar::getDayIdFromDate()
{
        updateDay();
        return dayId;
}

/*
 * Retourne le mois sous forme de string (janvier, février ...)
 */
const string Calendar::getMonthFromDate()
{
        if(month<1 || month>12)
                return M[0];
        return M[month];
}

void Calendar::setHours(int val)
{
        hours = val;
}

void Calendar::hoursDec()
{
        int res = (hours - 1) % 24;
        if (res == -1)
                res = 23;
        setHours(res);
}

void Calendar::hoursUp()
{
        setHours((hours + 1) % 24);
}

void Calendar::setMinutes(int val)
{
        minutes = val;
}

void Calendar::minutesDec()
{
        int res = (minutes - 1) % 60;
        if (res == -1)
                res = 59;
        setMinutes(res);
}

void Calendar::minutesUp()
{
        setMinutes((minutes + 1) % 60);
}

void Calendar::setSecondes(int val)
{
        secondes = val;
}

void Calendar::secondesDec()
{
        int res = (secondes - 1) % 60;
        if (res == -1)
                res = 59;
        setSecondes(res);
}

void Calendar::secondesUp()
{
        setSecondes((secondes + 1) % 60);
}

/*
 * Retourne le nombre de jours présent dans le mois
 */
int Calendar::getNbDaysInMonth()
{
        if(month<1 || month > 12)
                return 0;
        if (month == 2)
                return 28 + isleap(year);
        else
                return mounths[month];
}

/*
 * Retourne le nombre de jour dans le mois de février pour une année donnée
 */
bool Calendar::isleap(int n)
{
        if (n < 1732)
                return (!(n % 4));
        return (!(n % 4) && (n % 100)) || !(n % 400);
}

/*
 * Calcul le numéro du jour dans une semaine à partir d'une date
 */
void Calendar::updateDay()
{
        int m = month;
        int d = day;
        int y = year;
        //code from http://www.cppfrance.com/codes/JOUR-PARTIR-DATE-MM-JJ-YYYY_42179.aspx
        long L = 0;
        for (int i = 1; i < y; i++)
                L += 365 + isleap(i);
        for (int i = 1; i < m; i++)
        {
                if (i == 2)
                        L += 28 + isleap(y);
                else
                {
                        if(i<=12)
                                L += mounths[i];
                }
        }
        L += d + 4;
        L -= 11 * ((y > 1752) || (y == 1752 && m > 9) || (y == 1752 && m == 9 && d > 2));

        L %= 7;
        dayId = L;
}

void Calendar::monthUp()
{
        month = (month + 1) % 13;
        if (month == 0)
                month++;
        setMonth(month);
}

void Calendar::monthDown()
{
        month--;
        if (month == 0)
                month = 11;
        setMonth(month);
}

void Calendar::setMonth(int m)
{
        month = m;

        if (day > getNbDaysInMonth())
                setDay(getNbDaysInMonth());
}

void Calendar::yearUp()
{
        year++;
        setYear(year);
}

void Calendar::yearDown()
{
        year--;
        setYear(year);
}

void Calendar::setYear(int y)
{
        year = y;

        if (day > getNbDaysInMonth())
                setDay(getNbDaysInMonth());
}

void Calendar::dayUp()
{
        day++;
        if (day > getNbDaysInMonth())
                day = 1;
        setDay(day);
}

void Calendar::dayDown()
{
        day--;
        if (day == 0)
                day = getNbDaysInMonth();
        setYear(year);
}

void Calendar::setDay(int d)
{
        day = d;
}

/*
 * Retourne l'heure sous forme de chaine de caractère (01 02 03 ... 10 ...)
 */
string Calendar::hoursToString()
{
        string res = Utils::to_string(hours);
        if (hours < 10)
                res = "0" + res;
        return res;
}


/*
 * Retourne les minutes sous forme de chaine de caractère (01 02 03 ... 10 ...)
 */
string Calendar::minutesToString()
{
        string res = Utils::to_string(minutes);
        if (minutes < 10)
                res = "0" + res;
        return res;
}


/*
 * Retourne les secondes sous forme de chaine de caractère (01 02 03 ... 10 ...)
 */
string Calendar::secondesToString()
{
        string res = Utils::to_string(secondes);
        if (secondes < 10)
                res = "0" + res;
        return res;
}

/*
 * Applique l'heure du calendrier au système
 */
void Calendar::apply()
{
        char arg[50];

        if (exe)
                return;

        applyTimezone();
        sprintf(arg, "date %02d%02d%02d%02d%04d.%02d", month, day, hours, minutes, year, secondes);
        handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _CalendarHandle1, this);
        handler2 = handler;

        exe = ecore_exe_run(arg, NULL);
}

/*
 * Applique le fuseaux horaire du calendrier au système
 */
void Calendar::applyTimezone()
{
        std::ofstream _zone(TMP_CURRENT_ZONE);
        _zone << timeZone.timeZone[timeZone.current].key + "\n";
        _zone.close();

        ecore_file_mv(TMP_CURRENT_ZONE, CURRENT_ZONE);

        string new_ltime = ZONEPATH;
        new_ltime += timeZone.timeZone[timeZone.current].key;

        //copy the new zone info
        ecore_file_cp(new_ltime.c_str(), LOCALTIME);
}

void Calendar::setRestart(bool s)
{
        restart = s;
}

bool Calendar::isRestart()
{
        return restart;
}

Eina_Bool _CalendarHandle1(void *data, int type, void *event)
{
        Calendar *calendar = reinterpret_cast < Calendar * >(data);
        if (!calendar)
                return 0;

        ecore_event_handler_del(calendar->handler);
        //ecore_exe_free(calendar->exe);
        string cmd = "/sbin/hwclock --systohc";

        calendar->handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL, _CalendarHandle2, calendar);
        calendar->exe = ecore_exe_run(cmd.c_str(), NULL);

        return 0;
}

Eina_Bool _CalendarHandle2(void *data, int type, void *event)
{
        Calendar *calendar = reinterpret_cast < Calendar * >(data);
        if (!calendar)
                return 0;
        ecore_event_handler_del(calendar->handler);

        //ecore_exe_free(calendar->exe);
        Utils::logger("root") << Priority::INFO << "Calendar: Updating clock...  DONE" << log4cpp::eol;
        calendar->exe = NULL;
        //kill l'application
        //elle sera redémarrée par un daemon extérieur

        if (calendar->isRestart())
        {
                pid_t pid = getpid();
                kill(pid, SIGKILL);
        }

        return 0;
}
