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


class ScenarioAction
{
public:
    ScenarioAction()
    {}

    IOBase *io;
    std::string action;

    std::string toString()
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

    std::vector<ScenarioAction> actions;
    long int pause; //msec

    std::string toString()
    {
        std::string t = "\t\t[STEP] - pause:" + Utils::to_string(pause);
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

    std::string name;
    Room *room;
    bool visible;

    Params params;

    static const int END_STEP = 0xFEDC1234;

    std::vector<ScenarioStep> steps;
    ScenarioStep step_end;

    bool empty;

    json_t *createRequest();
    json_t *modifyRequest(IOBase *io);

    std::string toString()
    {
        std::string t = "[SCENARIO DATA] - name:" + name + " visible:" + Utils::to_string(visible) +
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

    Room *room = nullptr;
    EcoreTimer *timer = nullptr;

public:
    Scenario(CalaosConnection *c):
        connection(c)
    {}

    void load(json_t *jdata);

    IOBase *ioScenario = nullptr;
    IOBase *ioSchedule = nullptr;

    ScenarioData scenario_data;

    std::string getFirstCategory();

    bool isScheduled() { if (ioSchedule) return true; return false; }
    void createSchedule(sigc::slot<void, IOBase *> callback);
    void deleteSchedule();
    void setSchedules(TimeRangeInfos &tr);

    bool isScheduledDate(struct tm *scDate);

    //Return the room where the scenario is
    Room *getRoom();

    sigc::signal<void, Scenario *> load_done;
};

//small object for keeping a specific timerange and a scenario in sync
//mainly used for calendar view
class ScenarioSchedule
{
public:
    Scenario *scenario;
    int day = TimeRange::BADDAY;
    int timeRangeNum = 0;
};

class ScenarioModel: public sigc::trackable
{
private:
    CalaosConnection *connection;

    void scenario_list_cb(json_t *jdata, void *data);

    void notifyScenarioAdd(const std::string &msgtype, const Params &evdata);
    void notifyScenarioAddDelayed(const std::string &msgtype, const Params &evdata);
    void notifyScenarioDel(Scenario *sc);
    void notifyScenarioChange(const std::string &msgtype, const Params &evdata);

public:
    ScenarioModel(CalaosConnection *connection);
    ~ScenarioModel();

    void load(json_t *data);
    void createScenario(ScenarioData &data);
    void modifyScenario(Scenario *sc);
    void deleteScenario(Scenario *sc);

    std::list<Scenario *> scenarios;

    std::list<ScenarioSchedule> getScenarioForDate(struct tm scDate);

    sigc::signal<void> load_done;
    sigc::signal<void, Scenario *> scenario_new;
    sigc::signal<void, Scenario *> scenario_del;
    sigc::signal<void, Scenario *> scenario_change;
};

#endif // SCENARIOMODEL_H
