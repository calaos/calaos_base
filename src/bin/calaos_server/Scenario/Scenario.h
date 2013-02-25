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
#ifndef  SCENARIO_INC
#define  SCENARIO_INC

#include <Calaos.h>
#include <ScenarioIO.h>
#include <ListeRoom.h>
#include <ListeRule.h>
#include <IOFactory.h>
#include <ConditionStd.h>
#include <ActionStd.h>
#include <Room.h>
#include <InPlageHoraire.h>
#include <ScenarioStep.h>

using namespace Calaos;

namespace CalaosSimpleScenario
{
        class SimpleScenarioDate
        {
                public:
                int day;
                int month;
                int year;
                int hours;
                int min;
                int sec;
                int msec;

                SimpleScenarioDate():day(0),month(0),year(0),hours(0),min(0),sec(0),msec(0){;}
        };

        class SimpleScenario
        {
                private:
                        string name;

                        bool schedule;
                        bool cycle;

                        string roomName;
                        string roomType;

                        vector<SimpleScenarioStep> steps;
                        SimpleScenarioStep endStep;

                        vector<Horaire*> periodsLundi;
                        vector<Horaire*> periodsMardi;
                        vector<Horaire*> periodsMercredi;
                        vector<Horaire*> periodsJeudi;
                        vector<Horaire*> periodsVendredi;
                        vector<Horaire*> periodsSamedi;
                        vector<Horaire*> periodsDimanche;

                        vector<SimpleScenarioDate> dates;

                        /* The deleted scenario object. When modifying a scenario
                           we need to keep all rules using that object to work. So
                           changing this object everywhere in all rules make this work.
                         */
                        Output *ioScenario_to_del;

                        /** the scenario button */
                        string ioScenario;
                        /** true if the scenario is started */
                        string ioBoolean;
                        /** the next step number */
                        string ioInteger;
                        /** the timer */
                        string ioTimer;
                        /** the plage horaire */
                        string ioPlageHoraire;
                        /** the list of date io*/
                        vector<string> ioDate;
                public:
                        SimpleScenario();
                        ~SimpleScenario();

                        string getName() { return name; }
                        bool isSchedule() { return schedule; }
                        bool isCycle() { return cycle; }
                        vector<SimpleScenarioStep>& getSteps() { return steps; }
                        SimpleScenarioStep& getEndStep() { return endStep; }
                        void setEndStep( SimpleScenarioStep step) { endStep = step; }
                        vector<SimpleScenarioDate>& getDates() { return dates; }

                        string getIOScenario() { return ioScenario; }
                        string getIOBoolean() { return ioBoolean; }
                        string getIOInteger() { return ioInteger; }
                        string getIOTimer() { return ioTimer; }
                        vector<string>& getIODate() { return ioDate; }
                        string getIOPlageHoraire() { return ioPlageHoraire; }

                        string getRoomName() { return roomName; }
                        string getRoomType() { return roomType; }

                        void setIOScenario(string s) { ioScenario = s; }
                        void setIOBoolean(string s) { ioBoolean = s; }
                        void setIOInteger(string s) { ioInteger = s; }
                        void setIOTimer(string s) { ioTimer = s; }
                        void setIOPlageHoraire(string s) { ioPlageHoraire = s; }
                        void setIODate(vector<string> s) { ioDate = s; }

                        void setRoomName(string s) { roomName = s;}
                        void setRoomType(string s) { roomType = s;}

                        void changeRoomName(string s) { roomName = s; deleteIOButton(true); checkIOs(true);}
                        void changeRoomType(string s) { roomType = s; deleteIOButton(true); checkIOs(true);}

                        vector<Horaire*> * getPeriodsLundi() { return &periodsLundi; }
                        vector<Horaire*> * getPeriodsMardi() { return &periodsMardi; }
                        vector<Horaire*> * getPeriodsMercredi() { return &periodsMercredi; }
                        vector<Horaire*> * getPeriodsJeudi() { return &periodsJeudi; }
                        vector<Horaire*> * getPeriodsVendredi() { return &periodsVendredi; }
                        vector<Horaire*> * getPeriodsSamedi() { return &periodsSamedi; }
                        vector<Horaire*> * getPeriodsDimanche() { return &periodsDimanche; }


                        void setName(string s) {name =s;}
                        void updateName(string s);
                        void setSchedule(bool b) { schedule = b; }
                        void setCycle(bool b) { cycle = b; }

                        string toString();

                        void createIOs();
                        void deleteIOs();
                        void deleteIOButton(bool modify = false);
                        void deleteIOsDate();
                        void clear();
                        void createRules();
                        void createRules(SimpleScenarioStep &step, int idStep);
                        void deleteRules();
                        void clearPeriods();

                        void checkIOs(bool reuse_button = false);
        };
}

#endif

