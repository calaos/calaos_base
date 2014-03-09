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
#include <TCPConnection.h>
#include <CalaosConfig.h>
#include <Scenario.h>
#include <AutoScenario.h>

using namespace Calaos;

void TCPConnection::ScenarioCommand(Params &request, ProcessDone_cb callback)
{
    Params result = request;

    if (request["0"] != "scenario")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand() wrong command !";

        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);

        return;
    }

    if (request["1"] == "list")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand(list)";

        list<Scenario *> l = ListeRoom::Instance().getAutoScenarios();
        list<Scenario *>::iterator it = l.begin();

        for (int i = 2;it != l.end();it++, i++)
            result.Add(Utils::to_string(i), (*it)->get_param("id"));
    }
    else if (request["1"] == "get")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand(get)";

        Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(request["2"]));
        if (sc && sc->getAutoScenario())
        {
            AutoScenario *a = sc->getAutoScenario();
            int cpt = 3;
            result.Add(Utils::to_string(cpt), "cycle:" + string(a->isCycling()?"true":"false"));
            cpt++;
            result.Add(Utils::to_string(cpt), "enabled:" + string(a->isDisabled()?"false":"true"));
            cpt++;
            if (a->isScheduled())
                result.Add(Utils::to_string(cpt), string("schedule:") + a->getIOPlage()->get_param("id"));
            else
                result.Add(Utils::to_string(cpt), "schedule:false");
            cpt++;
            result.Add(Utils::to_string(cpt), "category:" + a->getCategory());
            cpt++;

            result.Add(Utils::to_string(cpt), "steps_count:" + Utils::to_string(a->getRuleSteps().size()));
            cpt++;

            for (uint i = 0;i < a->getRuleSteps().size();i++)
            {
                result.Add(Utils::to_string(cpt), "step:" + Utils::to_string(a->getStepPause(i)));
                cpt++;
                for (int j = 0;j < a->getStepActionCount(i);j++)
                {
                    ScenarioAction sa = a->getStepAction(i, j);
                    result.Add(Utils::to_string(cpt), sa.io->get_param("id") + ":" + sa.action);
                    cpt++;
                }
            }

            //End step
            result.Add(Utils::to_string(cpt), "step_end");
            cpt++;
            for (int j = 0;j < a->getEndStepActionCount();j++)
            {
                ScenarioAction sa = a->getEndStepAction(j);
                result.Add(Utils::to_string(cpt), sa.io->get_param("id") + ":" + sa.action);
                cpt++;
            }
        }
    }
    else if (request["1"] == "create")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand(create)";

        Params params;
        string room_name;
        Room *room = NULL;
        params.Add("auto_scenario", Calaos::get_new_scenario_id());
        int s = -1;
        Scenario *scenario = NULL;
        for (int i = 2;i < request.size();i++)
        {
            vector<string> tokens;
            split(request[Utils::to_string(i)], tokens, ":", 2);

            if (tokens[0] == "name") params.Add("name", url_decode(tokens[1]));
            else if (tokens[0] == "visible") params.Add("visible", tokens[1]);
            else if (tokens[0] == "room_name") room_name = url_decode(tokens[1]);
            else if (tokens[0] == "room_type") room = ListeRoom::Instance().searchRoomByNameAndType(room_name, url_decode(tokens[1]));
            else if (tokens[0] == "cycle") params.Add("cycle", tokens[1]);
            else if (tokens[0] == "disabled") params.Add("disabled", tokens[1]);
            else if (tokens[0] == "step")
            {
                if (!scenario)
                {
                    params.Add("type", "scenario");
                    if (!room) room = ListeRoom::Instance().get_room(0);
                    Input *in = ListeRoom::Instance().createInput(params, room);
                    scenario = dynamic_cast<Scenario *>(in);
                    scenario->getAutoScenario()->checkScenarioRules();

                    string sig = "new_scenario id:";
                    sig += scenario->get_param("id");
                    IPC::Instance().SendEvent("events", sig);
                }

                double pause;
                from_string(tokens[1], pause);

                s++;

                scenario->getAutoScenario()->addStep(pause);
            }
            else if (tokens[0] == "step_end")
            {
                if (!scenario)
                {
                    params.Add("type", "scenario");
                    if (!room) ListeRoom::Instance().get_room(0);
                    Input *in = ListeRoom::Instance().createInput(params, room);
                    scenario = dynamic_cast<Scenario *>(in);
                    scenario->getAutoScenario()->checkScenarioRules();

                    string sig = "new_scenario id:";
                    sig += scenario->get_param("id");
                    IPC::Instance().SendEvent("events", sig);
                }

                s = AutoScenario::END_STEP;
            }
            else if (s >= 0)
            {
                Output *out = ListeRoom::Instance().get_output(url_decode(tokens[0]));
                if (out)
                    scenario->getAutoScenario()->addStepAction(s, out, url_decode(tokens[1]));
            }
        }

        //Resave config, auto scenarios have probably created/deleted ios and rules
        Config::Instance().SaveConfigIO();
        Config::Instance().SaveConfigRule();
    }
    else if (request["1"] == "delete")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand(delete)";

        Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(request["2"]));
        if (sc && sc->getAutoScenario())
        {
            sc->getAutoScenario()->deleteAll();

            //delete the scenario IO
            ListeRoom::Instance().deleteIO(dynamic_cast<Input *>(sc));

            string sig = "delete_scenario id:";
            sig += request["2"];
            IPC::Instance().SendEvent("events", sig);

            //Resave config
            Config::Instance().SaveConfigIO();
            Config::Instance().SaveConfigRule();
        }
    }
    else if (request["1"] == "modify")
    {
        cDebugDom("network") << "TCPConnection::ScenarioCommand(modify)";

        Scenario *scenario = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(request["2"]));
        if (scenario && scenario->getAutoScenario())
        {
            scenario->getAutoScenario()->deleteRules();

            Params params;
            string room_name;
            Room *room = NULL;
            int s = -1;

            for (int i = 2;i < request.size();i++)
            {
                vector<string> tokens;
                split(request[Utils::to_string(i)], tokens, ":", 2);

                if (tokens[0] == "name") params.Add("name", url_decode(tokens[1]));
                else if (tokens[0] == "visible") params.Add("visible", tokens[1]);
                else if (tokens[0] == "room_name") room_name = url_decode(tokens[1]);
                else if (tokens[0] == "room_type") room = ListeRoom::Instance().searchRoomByNameAndType(room_name, url_decode(tokens[1]));
                else if (tokens[0] == "cycle") params.Add("cycle", tokens[1]);
                else if (tokens[0] == "disabled") params.Add("disabled", tokens[1]);
                else if (tokens[0] == "step")
                {
                    double pause;
                    from_string(tokens[1], pause);
                    s++;
                    scenario->getAutoScenario()->addStep(pause);
                }
                else if (tokens[0] == "step_end")
                {
                    s = AutoScenario::END_STEP;
                }
                else if (s >= 0)
                {
                    Output *out = ListeRoom::Instance().get_output(url_decode(tokens[0]));
                    if (out)
                        scenario->getAutoScenario()->addStepAction(s, out, url_decode(tokens[1]));
                }
            }

            //Check for changes
            if (params["name"] != scenario->get_param("name"))
            {
                scenario->set_param("name", params["name"]);

                string sig = "output ";
                sig += scenario->get_param("id") + " ";
                sig += url_encode("name" + string(":") + params["name"]) + " ";
                IPC::Instance().SendEvent("events", sig);

                sig = "input ";
                sig += scenario->get_param("id") + " ";
                sig += url_encode("name" + string(":") + params["name"]) + " ";
                IPC::Instance().SendEvent("events", sig);
            }

            if (params["visible"] != scenario->get_param("visible"))
            {
                scenario->set_param("visible", params["visible"]);

                string sig = "output ";
                sig += scenario->get_param("id") + " ";
                sig += url_encode("visible" + string(":") + params["visible"]) + " ";
                IPC::Instance().SendEvent("events", sig);

                sig = "input ";
                sig += scenario->get_param("id") + " ";
                sig += url_encode("visible" + string(":") + params["visible"]) + " ";
                IPC::Instance().SendEvent("events", sig);
            }

            Room *old_room = ListeRoom::Instance().getRoomByInput(scenario);
            if (room != old_room)
            {
                if (room)
                {
                    old_room->RemoveInputFromRoom(dynamic_cast<Input *>(scenario));
                    old_room->RemoveOutputFromRoom(dynamic_cast<Output *>(scenario));
                    room->AddInput(dynamic_cast<Input *>(scenario));
                    room->AddOutput(dynamic_cast<Output *>(scenario));

                    string sig = "modify_room ";
                    sig += url_encode(string("input_add:") + scenario->get_param("id")) + " ";
                    sig += url_encode(string("room_name:") + room->get_name()) + " ";
                    sig += url_encode(string("room_type:") + room->get_type());
                    IPC::Instance().SendEvent("events", sig);
                    sig = "modify_room ";
                    sig += url_encode(string("output_add:") + scenario->get_param("id")) + " ";
                    sig += url_encode(string("room_name:") + room->get_name()) + " ";
                    sig += url_encode(string("room_type:") + room->get_type());
                    IPC::Instance().SendEvent("events", sig);
                }
            }

            if (params["cycle"] != scenario->get_param("cycle"))
            {
                if (params["cycle"] == "true")
                    scenario->getAutoScenario()->setCycling(true);
                else
                    scenario->getAutoScenario()->setCycling(false);
            }

            if (params["disabled"] != scenario->get_param("disabled"))
            {
                if (params["disabled"] == "true")
                    scenario->getAutoScenario()->setDisabled(true);
                else
                    scenario->getAutoScenario()->setDisabled(false);
            }

            scenario->getAutoScenario()->checkScenarioRules();

            //Resave config
            Config::Instance().SaveConfigIO();
            Config::Instance().SaveConfigRule();

            string sig = "modify_scenario id:";
            sig += request["2"];
            IPC::Instance().SendEvent("events", sig);
        }
    }
    else if (request["1"] == "add_schedule")
    {
        cDebugDom("network") << "add_schedule";

        Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(request["2"]));
        if (sc && sc->getAutoScenario())
        {
            sc->getAutoScenario()->addSchedule();

            //Resave config
            Config::Instance().SaveConfigIO();
            Config::Instance().SaveConfigRule();

            result.clear();
            result.Add("0", "scenario");
            result.Add("1", request["2"]);
            result.Add("2", string("schedule_id:") + sc->getAutoScenario()->getIOPlage()->get_param("id"));
        }
    }
    else if (request["1"] == "del_schedule")
    {
        cDebugDom("network") << "add_schedule";

        Scenario *sc = dynamic_cast<Scenario *>(ListeRoom::Instance().get_input(request["2"]));
        if (sc && sc->getAutoScenario())
        {
            sc->getAutoScenario()->deleteSchedule();

            //Resave config
            Config::Instance().SaveConfigIO();
            Config::Instance().SaveConfigRule();

            result.clear();
            result.Add("0", "scenario");
            result.Add("1", "ok");
        }
    }

    ProcessDone_signal sig;
    sig.connect(callback);
    sig.emit(result);
}
