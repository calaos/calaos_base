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
#include "ScenarioModel.h"
#include "CalaosModel.h"

ScenarioModel::ScenarioModel(CalaosConnection *con):
    connection(con)
{
    connection->notify_scenario_add.connect(
                sigc::mem_fun(*this, &ScenarioModel::notifyScenarioAdd));
    connection->notify_scenario_change.connect(
                sigc::mem_fun(*this, &ScenarioModel::notifyScenarioChange));
}

ScenarioModel::~ScenarioModel()
{
    for_each(scenarios.begin(), scenarios.end(), Delete());
}

void ScenarioModel::load(json_t *data)
{
    connection->sendCommand("autoscenario", {{ "type", "list" }}, sigc::mem_fun(*this, &ScenarioModel::scenario_list_cb));
}

void ScenarioModel::scenario_list_cb(json_t *jdata, void *data)
{
    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "scenarios"), idx, value)
    {
        string id = jansson_string_get(value, "id");
        map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(id);

        if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
        {
            cErrorDom("scenario") << "Unknown Input \'" << id << "\' !";
            continue;
        }

        Scenario *sc = new Scenario(connection);
        sc->ioScenario = it->second;
        sc->scenario_data.empty = false;
        sc->scenario_data.params.Add("id", id);
        sc->ioScenario->io_deleted.connect(sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioDel), sc));
        sc->load(value);
        scenarios.push_back(sc);
    }

    load_done.emit();
}

void Scenario::load(json_t *jdata)
{
    scenario_data.name = ioScenario->params["name"];
    if (ioScenario->params["visible"] == "true")
    {
        scenario_data.visible = true;
        scenario_data.room = ioScenario->getRoom();
    }

    jansson_decode_object(jdata, scenario_data.params);

    if (scenario_data.params["schedule"] != "false")
    {
        map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(scenario_data.params["schedule"]);
        if (it != CalaosModel::Instance().getHome()->getCacheInputs().end())
        {
            ioSchedule = (*it).second;
            ioSchedule->io_deleted.connect([=]()
            {
                ioSchedule = nullptr;
            });
        }
    }

    size_t idx;
    json_t *value;

    json_array_foreach(json_object_get(jdata, "steps"), idx, value)
    {
        int index_act;

        if (jansson_string_get(value, "step_type") == "standard")
        {
            ScenarioStep sstep;
            from_string(jansson_string_get(value, "step_pause"), sstep.pause);
            scenario_data.steps.push_back(sstep);

            index_act = idx;
        }
        else
        {
            index_act = ScenarioData::END_STEP;
        }

        size_t idx_act;
        json_t *value_act;

        json_array_foreach(json_object_get(value, "actions"), idx_act, value_act)
        {
            string id_out = jansson_string_get(value_act, "id");
            string act = jansson_string_get(value_act, "action");

            map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheOutputs().find(id_out);
            if (it == CalaosModel::Instance().getHome()->getCacheOutputs().end())
            {
                cErrorDom("scenario") << "Unknown action id \'" << id_out << "\' with action \'" << act << "\' !";
                continue;
            }
            IOBase *io = (*it).second;

            IOActionList ac = io->getActionListFromAction(act);
            ScenarioAction sa;
            sa.io = io;
            sa.action = ac.getComputedAction(io);

            if (index_act == ScenarioData::END_STEP)
                scenario_data.step_end.actions.push_back(sa);
            else
                scenario_data.steps[index_act].actions.push_back(sa);
        }
    }
}

Room *Scenario::getRoom()
{
    if (!room && ioScenario && ioScenario->getRoom())
        room = ioScenario->getRoom();

    return room;
}

string Scenario::getFirstCategory()
{
    vector<string> tok;
    split(scenario_data.params["category"], tok, "-");
    if (tok.size() > 0)
        return tok[0];

    return "other";
}

json_t *ScenarioData::createRequest()
{
    json_t *jret = json_object();

    json_object_set_new(jret, "type", json_string("create"));
    json_object_set_new(jret, "name", json_string(name.c_str()));
    json_object_set_new(jret, "visible", json_string(visible?"true":"false"));
    if (visible)
    {
        json_object_set_new(jret, "room_name", json_string(room->name.c_str()));
        json_object_set_new(jret, "room_type", json_string(room->type.c_str()));
    }
    json_object_set_new(jret, "cycle", json_string(params["cycle"].c_str()));

    json_t *jsteps = json_array();

    for (uint i = 0;i < steps.size();i++)
    {
        json_t *jstep = json_object();
        ScenarioStep &step = steps[i];

        json_object_set_new(jstep, "step_pause", json_string(Utils::to_string(step.pause).c_str()));
        json_object_set_new(jstep, "step_type", json_string("standard"));

        json_t *jacts = json_array();
        for (uint j = 0;j < step.actions.size();j++)
        {
            ScenarioAction &sa = step.actions[j];

            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->params["id"].c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }

        json_object_set_new(jstep, "actions", jacts);

        json_array_append_new(jsteps, jstep);
    }

    if (!step_end.actions.empty())
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_type", json_string("end"));

        json_t *jacts = json_array();
        for (uint j = 0;j < step_end.actions.size();j++)
        {
            ScenarioAction &sa = step_end.actions[j];
            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->params["id"].c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }
        json_object_set_new(jstep, "actions", jacts);
        json_array_append_new(jsteps, jstep);
    }

    json_object_set_new(jret, "steps", jsteps);

    return jret;
}

json_t *ScenarioData::modifyRequest(IOBase *io)
{
    json_t *jret = json_object();

    json_object_set_new(jret, "type", json_string("modify"));
    json_object_set_new(jret, "id", json_string(io->params["id"].c_str()));
    json_object_set_new(jret, "name", json_string(name.c_str()));
    json_object_set_new(jret, "visible", json_string(visible?"true":"false"));
    if (visible)
    {
        json_object_set_new(jret, "room_name", json_string(room->name.c_str()));
        json_object_set_new(jret, "room_type", json_string(room->type.c_str()));
    }
    json_object_set_new(jret, "cycle", json_string(params["cycle"].c_str()));

    json_t *jsteps = json_array();

    for (uint i = 0;i < steps.size();i++)
    {
        json_t *jstep = json_object();
        ScenarioStep &step = steps[i];

        json_object_set_new(jstep, "step_pause", json_string(Utils::to_string(step.pause).c_str()));
        json_object_set_new(jstep, "step_type", json_string("standard"));

        json_t *jacts = json_array();
        for (uint j = 0;j < step.actions.size();j++)
        {
            ScenarioAction &sa = step.actions[j];

            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->params["id"].c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }

        json_object_set_new(jstep, "actions", jacts);

        json_array_append_new(jsteps, jstep);
    }

    if (!step_end.actions.empty())
    {
        json_t *jstep = json_object();
        json_object_set_new(jstep, "step_type", json_string("end"));

        json_t *jacts = json_array();
        for (uint j = 0;j < step_end.actions.size();j++)
        {
            ScenarioAction &sa = step_end.actions[j];
            json_t *jact = json_object();
            json_object_set_new(jact, "id", json_string(sa.io->params["id"].c_str()));
            json_object_set_new(jact, "action", json_string(sa.action.c_str()));
            json_array_append_new(jacts, jact);
        }
        json_object_set_new(jstep, "actions", jacts);
        json_array_append_new(jsteps, jstep);
    }

    json_object_set_new(jret, "steps", jsteps);

    return jret;
}

void Scenario::createSchedule(sigc::slot<void, IOBase *> callback)
{
    connection->sendCommand("autoscenario", {{ "type", "add_schedule" },
                                             { "id", ioScenario->params["id"] }},
                            [=](json_t *jdata, void *)
    {
        string sched_id = jansson_string_get(jdata, "id");

        if (sched_id.empty())
        {
            callback(nullptr);
            return;
        }

        double start_time = ecore_time_get();

        //We need to delay a bit because we have to wait for RoomModel to load the io id first
        timer = new EcoreTimer(0.05, [=]()
        {
            map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(sched_id);

            if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
            {
                if (ecore_time_get() - start_time >= 5.0)
                {
                    cErrorDom("scenario") << "I was not able to find input \'" << sched_id << "\' for 5s! This is bad... Giving up.";
                    delete timer;

                    callback(nullptr);
                }
                else
                {
                    cDebugDom("scenario") << "Still waiting for input \'" << sched_id << "\'....";
                }

                return;
            }

            DELETE_NULL(timer);

            ioSchedule = it->second;
            ioSchedule->io_deleted.connect([=]()
            {
                ioSchedule = nullptr;
            });

            callback(ioSchedule);
        });
    });
}

void Scenario::deleteSchedule()
{
    connection->sendCommand("autoscenario", {{ "type", "del_schedule" }, { "id", ioScenario->params["id"] }});
    ioSchedule = nullptr;
}

void Scenario::setSchedules(TimeRangeInfos &tr)
{
    if (!ioSchedule)
    {
        cCriticalDom("scenario") << "called with ioPlage == null!";
        return;
    }

    cDebugDom("scenario") << "Saving Scenario schedule: ";
    cout << tr.toString();

    json_t *jret = json_object();

    json_object_set_new(jret, "id", json_string(ioSchedule->params["id"].c_str()));

    //Send months
    stringstream ssmonth;
    ssmonth << tr.range_months;
    string str = ssmonth.str();
    std::reverse(str.begin(), str.end());

    json_object_set_new(jret, "months", json_string(str.c_str()));
    json_t *jranges = json_array();

    auto addRange = [=](const vector<TimeRange> &ranges, int day)
    {
        for (const TimeRange &t: ranges)
            json_array_append_new(jranges, t.toParams(day).toJson());
    };

    addRange(tr.range_monday, 0);
    addRange(tr.range_tuesday, 1);
    addRange(tr.range_wednesday, 2);
    addRange(tr.range_thursday, 3);
    addRange(tr.range_friday, 4);
    addRange(tr.range_saturday, 5);
    addRange(tr.range_sunday, 6);

    json_object_set_new(jret, "ranges", jranges);

    connection->sendJson("set_timerange", jret);
}

void ScenarioModel::createScenario(ScenarioData &data)
{
    connection->sendJson("autoscenario", data.createRequest());
}

void ScenarioModel::modifyScenario(Scenario *sc)
{
    if (!sc || !sc->ioScenario) return;
    connection->sendJson("autoscenario", sc->scenario_data.modifyRequest(sc->ioScenario));
}

void ScenarioModel::deleteScenario(Scenario *sc)
{
    if (!sc || !sc->ioScenario) return;

    connection->sendCommand("autoscenario", {{ "type", "delete" }, { "id", sc->ioScenario->params["id"] }});
}

void ScenarioModel::notifyScenarioAdd(const string &msgtype, const Params &evdata)
{
    cDebugDom("scenario") << "New scenario notif, start timer to load scenario data...";

    //We need to delay the load of the scenario because we have to wait for RoomModel to load the scenario id first
    EcoreTimer::singleShot(0.5, sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioAddDelayed), msgtype, evdata));
}

void ScenarioModel::notifyScenarioAddDelayed(const string &msgtype, const Params &evdata)
{
    VAR_UNUSED(msgtype);
    cDebugDom("scenario") << "New scenario, load data";

    map<string, IOBase *>::const_iterator it = CalaosModel::Instance().getHome()->getCacheInputs().find(evdata["id"]);

    if (it == CalaosModel::Instance().getHome()->getCacheInputs().end())
    {
        cErrorDom("scenario") << "Unknown Input \'" << evdata["id"] << "\' !";
        return;
    }

    Scenario *sc = new Scenario(connection);
    sc->ioScenario = it->second;
    sc->scenario_data.empty = false;
    sc->ioScenario->io_deleted.connect(sigc::bind(sigc::mem_fun(*this, &ScenarioModel::notifyScenarioDel), sc));
    sc->scenario_data.params.Add("id", evdata["id"]);
    scenarios.push_back(sc);

    connection->sendCommand("autoscenario", {{ "type", "get" },
                                             { "id", evdata["id"] }},
                            [=](json_t *jdata, void *)
    {
        sc->load(jdata);

        cDebugDom("scenario") << "New scenario, load done, emit signal";
        scenario_new.emit(sc);
    });
}

void ScenarioModel::notifyScenarioDel(Scenario *sc)
{
    if (!sc)
    {
        cErrorDom("scenario") << "sc is NULL!";
        return;
    }

    list<Scenario *>::iterator it;
    it = find(scenarios.begin(), scenarios.end(), sc);

    if (it == scenarios.end())
    {
        cErrorDom("scenario") << "Unknown scenario \'" << sc->ioScenario->params["id"] << "\' !";
        return;
    }

    scenario_del.emit(*it);

    delete *it;
    scenarios.erase(it);
}

void ScenarioModel::notifyScenarioChange(const string &msgtype, const Params &evdata)
{
    VAR_UNUSED(msgtype);
    cDebug() << "scenario id: " << evdata["id"] << " changed, broadcasting signal";

    list<Scenario *>::iterator it = scenarios.begin();
    for (;it != scenarios.end();it++)
    {
        Scenario *sc = *it;
        if (sc->ioScenario && sc->ioScenario->params["id"] == evdata["id"])
        {
            cDebugDom("scenario") << "Reload scenario " << evdata["id"];

            connection->sendCommand("autoscenario", {{ "type", "get" },
                                                     { "id", evdata["id"] }},
                                    [=](json_t *jdata, void *)
            {
                //clear scenario data before reloading them again
                sc->scenario_data = ScenarioData();
                sc->scenario_data.params.Add("id", evdata["id"]);
                sc->ioSchedule = nullptr;

                sc->load(jdata);

                scenario_change.emit(sc);
            });

            break;
        }
    }
}

list<ScenarioSchedule> ScenarioModel::getScenarioForDate(struct tm scDate)
{
    list<ScenarioSchedule> retList;

    auto it = scenarios.begin();
    for (;it != scenarios.end();it++)
    {
        Scenario *sc = *it;

        cDebugDom("scenario") << "Checking scenario: " << sc->ioScenario->params["name"];

        if (!sc->isScheduled())
            continue;

        cDebugDom("scenario") << "Scenario schedule: ";
        cout << sc->ioSchedule->range_infos.toString();

        auto checkScenario = [=,&retList](int day)
        {
            cDebugDom("scenario") << "Checking day: " << day;
            vector<int> num;
            num = sc->ioSchedule->range_infos.isScheduledDate(scDate);

            for (uint i = 0;i < num.size();i++)
            {
                ScenarioSchedule s;
                s.scenario = sc;
                s.day = day;
                s.timeRangeNum = num[i];
                retList.push_back(s);
            }
        };

        switch (scDate.tm_wday)
        {
        case 1: checkScenario(TimeRange::MONDAY); break;
        case 2: checkScenario(TimeRange::TUESDAY); break;
        case 3: checkScenario(TimeRange::WEDNESDAY); break;
        case 4: checkScenario(TimeRange::THURSDAY); break;
        case 5: checkScenario(TimeRange::FRIDAY); break;
        case 6: checkScenario(TimeRange::SATURDAY); break;
        case 0: checkScenario(TimeRange::SUNDAY); break;
        default: break;
        }
    }

    retList.sort([](const ScenarioSchedule &a, const ScenarioSchedule &b)
    {
        long timea = 0, timeb = 0;

        vector<TimeRange> vtr = a.scenario->ioSchedule->range_infos.getRange(a.day);
        if (a.timeRangeNum >= 0 && a.timeRangeNum < (int)vtr.size())
            timea = vtr[a.timeRangeNum].getStartTimeSec();

        vtr = b.scenario->ioSchedule->range_infos.getRange(b.day);
        if (b.timeRangeNum >= 0 && b.timeRangeNum < (int)vtr.size())
            timeb = vtr[b.timeRangeNum].getStartTimeSec();

        return timea < timeb;
    });

    return retList;
}
