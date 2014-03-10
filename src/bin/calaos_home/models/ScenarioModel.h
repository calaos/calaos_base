/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef SCENARIOMODEL_H
#define SCENARIOMODEL_H

#include <Utils.h>

#include "CalaosConnection.h"
#include "RoomModel.h"

using namespace Utils;

class ScenarioAction
{
public:
    ScenarioAction()
    {}

    IOBase *io;
    string action;

    string toString()
    {
        if (!io) return "Empty action !";
        return io->params["id"] + " : " + action;
    }

};

class ScenarioStep
{
public:
    ScenarioStep():
        pause(1000)
    {}

    vector<ScenarioAction> actions;
    long int pause; //msec

    string toString()
    {
        string t = "\t\t[STEP] - pause:" + Utils::to_string(pause);
        for (uint i = 0;i < actions.size();i++)
            t += "\n\t\t\t" + actions[i].toString();
        return t;
    }
};

class ScenarioData
{
public:
    ScenarioData():
        room(NULL),
        visible(false),
        empty(true)
    {
        params.Add("cycle", "false");
        params.Add("enabled", "true");
    }

    string name;
    Room *room;
    bool visible;

    Params params;

    static const int END_STEP = 0xFEDC1234;

    vector<ScenarioStep> steps;
    ScenarioStep step_end;

    bool empty;

    string createRequest();
    string modifyRequest(IOBase *io);

    string toString()
    {
        string t = "[SCENARIO DATA] - name:" + name + " visible:" + Utils::to_string(visible) +
                   "\n" + params.toString();
        for (uint i = 0;i < steps.size();i++)
            t += "\n\t[Step " + Utils::to_string(i) + "]\n" + steps[i].toString();
        t += "\n\t[Step End]\n" + step_end.toString();
        return t;
    }
};

class Scenario: public sigc::trackable
{
private:
    CalaosConnection *connection;

    Room *room;

public:
    Scenario(CalaosConnection *c):
        connection(c),
        room(NULL),
        ioScenario(NULL),
        ioPlage(NULL)
    {}

    void scenario_get_cb(bool success, vector<string> result, void *data);

    IOBase *ioScenario;
    IOBase *ioPlage;

    ScenarioData scenario_data;

    string getFirstCategory();

    bool isScheduled() { if (ioPlage) return true; return false; }
    void createSchedule(sigc::slot<void, IOBase *> callback);
    void deleteSchedule();
    void setSchedules(TimeRangeInfos &tr);

    //Return the room where the scenario is
    Room *getRoom();

    sigc::signal<void, Scenario *> load_done;
};

class ScenarioModel: public sigc::trackable
{
private:
    CalaosConnection *connection;

    int load_count;
    void load_scenario_done(Scenario *sc);
    void load_new_scenario_done(Scenario *sc);

    void scenario_list_cb(bool success, vector<string> result, void *data);

    void notifyScenarioAdd(string notif);
    void notifyScenarioAddDelayed(string notif);
    void notifyScenarioDel(Scenario *sc);
    void notifyScenarioChange(string notif);

public:
    ScenarioModel(CalaosConnection *connection);
    ~ScenarioModel();

    void load();
    void createScenario(ScenarioData &data);
    void modifyScenario(Scenario *sc);
    void deleteScenario(Scenario *sc);

    list<Scenario *> scenarios;

    sigc::signal<void> load_done;
    sigc::signal<void, Scenario *> scenario_new;
    sigc::signal<void, Scenario *> scenario_del;
    sigc::signal<void, Scenario *> scenario_change;
};

#endif // SCENARIOMODEL_H
