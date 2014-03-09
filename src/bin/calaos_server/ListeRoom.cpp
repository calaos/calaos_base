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
#include <ListeRoom.h>
#include <IPC.h>
#include <AutoScenario.h>
#include <CalaosConfig.h>

using namespace Calaos;

ListeRoom &ListeRoom::Instance()
{
    static ListeRoom inst;

    return inst;
}

ListeRoom::ListeRoom()
{
    eina_init();

    input_table = eina_hash_string_superfast_new(NULL);
    output_table = eina_hash_string_superfast_new(NULL);

    cDebugDom("rooms");
}

ListeRoom::~ListeRoom()
{
    for (uint i = 0;i < rooms.size();i++)
        delete rooms[i];

    rooms.clear();

    eina_hash_free(input_table);
    eina_hash_free(output_table);

    eina_shutdown();

    cDebugDom("room");
}

void ListeRoom::addInputHash(Input *input)
{
    if (!input) return;

    string id = input->get_param("id");
    if (input->get_param("type") == "CamInput" ||
        input->get_param("type") == "AudioInput")
    {
        id = input->get_param("iid");
    }

    cDebugDom("room") << id;

    eina_hash_add(input_table, id.c_str(), input);
}

void ListeRoom::delInputHash(Input *input)
{
    if (!input) return;

    string id = input->get_param("id");
    if (input->get_param("type") == "CamInput" ||
        input->get_param("type") == "AudioInput")
    {
        id = input->get_param("iid");
    }

    cDebugDom("room") << id;

    eina_hash_del(input_table, id.c_str(), NULL);
}

void ListeRoom::addOutputHash(Output *output)
{
    if (!output) return;

    string id = output->get_param("id");
    if (output->get_param("type") == "CamOutput" ||
        output->get_param("type") == "AudioOutput")
    {
        id = output->get_param("oid");
    }

    cDebugDom("room") << id;

    eina_hash_add(output_table, id.c_str(), output);
}

void ListeRoom::delOutputHash(Output *output)
{
    if (!output) return;

    string id = output->get_param("id");
    if (output->get_param("type") == "CamOutput" ||
        output->get_param("type") == "AudioOutput")
    {
        id = output->get_param("oid");
    }

    cDebugDom("room") << id;

    eina_hash_del(output_table, id.c_str(), NULL);
}

void ListeRoom::Add(Room *p)
{
    rooms.push_back(p);

    cDebugDom("room") << p->get_name() << "," << p->get_type();
}

void ListeRoom::Remove(int pos)
{
    vector<Room *>::iterator iter = rooms.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    delete rooms[pos];
    rooms.erase(iter);

    cDebugDom("room");
}

Room *ListeRoom::operator[] (int i) const
{
    return rooms[i];
}

Room *ListeRoom::get_room(int i)
{
    return rooms[i];
}

Input *ListeRoom::get_input(std::string in)
{
    Input *i = reinterpret_cast<Input *>(eina_hash_find(input_table, in.c_str()));

    return i;
}

Output *ListeRoom::get_output(std::string out)
{
    Output *o = reinterpret_cast<Output *>(eina_hash_find(output_table, out.c_str()));

    return o;
}

Input *ListeRoom::get_input(int i)
{
    int cpt = 0;

    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size_in();m++)
        {
            Input *in = rooms[j]->get_input(m);
            if (cpt == i)
            {
                return in;
            }

            cpt++;
        }
    }

    return NULL;
}

Output *ListeRoom::get_output(int i)
{
    int cpt = 0;

    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size_out();m++)
        {
            Output *out = rooms[j]->get_output(m);
            if (cpt == i)
            {
                return out;
            }

            cpt++;
        }
    }

    return NULL;
}

bool ListeRoom::delete_input(Input *input, bool del)
{
    bool done = false;
    for (uint j = 0;!done && j < rooms.size();j++)
    {
        for (int m = 0;!done && m < get_room(j)->get_size_in();m++)
        {
            Input *in = get_room(j)->get_input(m);
            if (in == input)
            {
                get_room(j)->RemoveInput(m, del);
                done = true;
            }
        }
    }

    return done;
}

bool ListeRoom::delete_output(Output *output, bool del)
{
    bool done = false;
    for (uint j = 0;!done && j < rooms.size();j++)
    {
        for (int m = 0;!done && m < get_room(j)->get_size_out();m++)
        {
            Output *out = get_room(j)->get_output(m);
            if (out == output)
            {
                get_room(j)->RemoveOutput(m, del);
                done = true;
            }
        }
    }

    return done;
}

int ListeRoom::get_nb_input()
{
    return eina_hash_population(input_table);
}

int ListeRoom::get_nb_output()
{
    return eina_hash_population(output_table);
}

Input *ListeRoom::get_chauffage_var(std::string &chauff_id, ChauffType type)
{
    for (uint j = 0;j < rooms.size();j++)
    {
        for (int m = 0;m < rooms[j]->get_size_in();m++)
        {
            Input *in = rooms[j]->get_input(m);
            if (in->get_param("chauffage_id") == chauff_id)
            {
                switch (type)
                {
                case PLAGE_HORAIRE: if (in->get_param("type") == "InPlageHoraire") return in; break;
                case CONSIGNE: if (in->get_param("type") == "InternalInt") return in; break;
                case ACTIVE: if (in->get_param("type") == "InternalBool") return in; break;
                }
            }
        }
    }

    return NULL;
}

void ListeRoom::addScenarioCache(Scenario *sc)
{
    auto_scenario_cache.push_back(sc);
}

void ListeRoom::delScenarioCache(Scenario *sc)
{
    auto_scenario_cache.remove(sc);
}

list<Scenario *> ListeRoom::getAutoScenarios()
{
    cDebugDom("room") << "Found " << auto_scenario_cache.size() << " auto_scenarios.";

    return auto_scenario_cache;
}

void ListeRoom::checkAutoScenario()
{
    list<Scenario *>::iterator it = auto_scenario_cache.begin();

    for (;it != auto_scenario_cache.end();it++)
    {
        Scenario *sc = *it;
        if (sc->getAutoScenario())
            sc->getAutoScenario()->checkScenarioRules();
    }

    list<Rule *> to_remove;
    for (int i = 0;i < ListeRule::Instance().size();i++)
    {
        Rule *rule = ListeRule::Instance().get_rule(i);
        if (rule->param_exists("auto_scenario") && !rule->isAutoScenario())
            to_remove.push_back(rule);
    }

    list<Rule *>::iterator itr = to_remove.begin();
    for (;itr != to_remove.end();itr++)
        ListeRule::Instance().Remove(*itr);

    //Resave config, auto scenarios have probably created/deleted ios and rules
    Config::Instance().SaveConfigIO();
    Config::Instance().SaveConfigRule();
}

Room * ListeRoom::searchRoomByNameAndType(string name, string type)
{
    Room *r = NULL;
    vector<Room *>::iterator itRoom;

    for(itRoom = rooms.begin(); itRoom != rooms.end() && !r; itRoom++)
        if( (*itRoom)->get_name() == name && (*itRoom)->get_type() == type)
            r = *itRoom;

    return r;
}

Room *ListeRoom::getRoomByInput(Input *o)
{
    Room *r = NULL;

    for (uint j = 0;j < rooms.size() && !r;j++)
    {
        for (int m = 0;m < rooms[j]->get_size_in() && !r;m++)
        {
            if (rooms[j]->get_input(m) == o)
                r = rooms[j];
        }
    }

    return r;
}

Room *ListeRoom::getRoomByOutput(Output *o)
{
    Room *r = NULL;

    for (uint j = 0;j < rooms.size() && !r;j++)
    {
        for (int m = 0;m < rooms[j]->get_size_out() && !r;m++)
        {
            if (rooms[j]->get_output(m) == o)
                r = rooms[j];
        }
    }

    return r;
}

bool ListeRoom::deleteIO(Input *input, bool modify)
{
    //first delete all rules using "input"
    if (!modify) //only deletes if modify is not set
        ListeRule::Instance().RemoveRule(input);

    bool done = false;
    if (input->get_param("type") == "CamInput"
        || input->get_param("type") == "AudioInput"
        || input->get_param("type") == "IRInput"
        || input->get_param("type") == "CamOutput"
        || input->get_param("type") == "AudioOutput"
        || input->get_param("type") == "IROutput")
    {
        CamInput *icam = dynamic_cast<CamInput *>(input);
        if (icam)
        {
            IPCam *cam = icam->get_cam();
            delete_input(cam->get_input(), false);
            if (!modify) //only deletes if modify is not set
                ListeRule::Instance().RemoveRule(cam->get_output());
            delete_output(cam->get_output(), false);
            CamManager::Instance().Delete(cam);
            done = true;
        }
        AudioInput *iaudio = dynamic_cast<AudioInput *>(input);
        if (iaudio)
        {
            AudioPlayer *audio = iaudio->get_player();
            delete_input(audio->get_input(), false);
            if (!modify) //only deletes if modify is not set
                ListeRule::Instance().RemoveRule(audio->get_output());
            delete_output(audio->get_output(), false);
            AudioManager::Instance().Delete(audio);
            done = true;
        }
    }
    else
    {
        if (input->get_param("type") == "InputTime"
            || input->get_param("type") == "WITemp"
            || input->get_param("type") == "WIAnalog"
            || input->get_param("type") == "OWTemp"
            || input->get_param("type") == "InPlageHoraire"
            || input->get_param("type") == "InputTimer")
            ListeRule::Instance().Remove(input);
        if (input->get_param("type") == "InputTimer")
        {
            //also delete the output
            InputTimer *tm = dynamic_cast<InputTimer *> (input);
            Output *o = dynamic_cast<Output *> (tm);
            if (!modify) //only deletes if modify is not set
                ListeRule::Instance().RemoveRule(o);
            if (o) ListeRoom::Instance().delete_output(o, false);
        }

        if (input->get_param("type") == "scenario" || input->get_param("type") == "Scenario")
        {
            //also delete the output
            Scenario *sc = dynamic_cast<Scenario *> (input);
            if (sc)
            {
                Output *o = dynamic_cast<Output *> (sc);
                if (!modify) //only deletes if modify is not set
                    ListeRule::Instance().RemoveRule(o);
                if (o)
                    ListeRoom::Instance().delete_output(o, false);
            }
        }

        if (input->get_param("type") == "InternalBool" ||
            input->get_param("type") == "InternalInt" ||
            input->get_param("type") == "InternalString")
        {
            //also delete the output
            Internal *internal = dynamic_cast<Internal *> (input);

            if (internal)
            {
                Output *o = dynamic_cast<Output *> (internal);
                if (!modify) //only deletes if modify is not set
                    ListeRule::Instance().RemoveRule(o);
                if (o)
                    ListeRoom::Instance().delete_output(o, false);
            }
        }

        done = ListeRoom::Instance().delete_input(input);
    }

    return done;
}


bool ListeRoom::deleteIO(Output *output, bool modify)
{
    //first delete all rules using "output"
    if (!modify) //only deletes if modify is not set
        ListeRule::Instance().RemoveRule(output);

    bool done = false;
    if (output->get_param("type") == "CamInput"
        || output->get_param("type") == "AudioInput"
        || output->get_param("type") == "IRInput"
        || output->get_param("type") == "CamOutput"
        || output->get_param("type") == "AudioOutput"
        || output->get_param("type") == "IROutput")
    {
        CamOutput *icam = dynamic_cast<CamOutput *>(output);
        if (icam)
        {
            IPCam *cam = icam->get_cam();
            ListeRule::Instance().RemoveRule(cam->get_input());
            ListeRoom::Instance().delete_input(cam->get_input(), false);
            ListeRoom::Instance().delete_output(cam->get_output(), false);
            CamManager::Instance().Delete(cam);
            done = true;
        }
        AudioOutput *iaudio = dynamic_cast<AudioOutput *>(output);
        if (iaudio)
        {
            AudioPlayer *audio = iaudio->get_player();
            ListeRule::Instance().RemoveRule(audio->get_input());
            ListeRoom::Instance().delete_input(audio->get_input(), false);
            ListeRoom::Instance().delete_output(audio->get_output(), false);
            AudioManager::Instance().Delete(audio);
            done = true;
        }
    }
    else if (output->get_param("type") != "OutTouchscreen")
    {
        if (output->get_param("type") == "InputTimer")
        {
            //also delete the input
            InputTimer *tm = dynamic_cast<InputTimer *> (output);
            Input *in = dynamic_cast<Input *> (tm);
            ListeRule::Instance().RemoveRule(in);
            ListeRule::Instance().Remove(in);
            if (in) ListeRoom::Instance().delete_input(in, false);
        }

        if (output->get_param("type") == "scenario"
            || output->get_param("type") == "Scenario")
        {
            //also delete the input
            Scenario *sc = dynamic_cast<Scenario *> (output);
            if (sc)
            {
                Input *o = dynamic_cast<Input *> (sc);
                ListeRule::Instance().RemoveRule(o);
                if (o) ListeRoom::Instance().delete_input(o, false);
            }
        }

        if (output->get_param("type") == "InternalBool" ||
            output->get_param("type") == "InternalInt" ||
            output->get_param("type") == "InternalString")
        {
            //also delete the input
            Internal *internal = dynamic_cast<Internal *> (output);
            if (internal)
            {
                Input *o = dynamic_cast<Input *> (internal);
                if (o)
                {
                    ListeRule::Instance().RemoveRule(o);
                    ListeRoom::Instance().delete_input(o, false);
                }
            }
        }

        done = ListeRoom::Instance().delete_output(output);
    }

    return done;
}

Input* ListeRoom::createInput(Params param, Room *room)
{
    Input *input = NULL;

    if (!param.Exists("name")) param.Add("name", "Input");
    if (!param.Exists("type")) param.Add("type", "WIDigital");
    if (param["type"] != "scenario" && param["type"] != "InputTimeDate" &&
        param["type"] != "InputTime" && param["type"] != "X10Output" &&
        param["type"] != "InternalInt" && param["type"] != "InternalBool" &&
        param["type"] != "InternalString" && param["type"] != "InputTimer")
    {
        if (!param.Exists("var")) param.Add("var", "0");
        if (!param.Exists("host")) param.Add("host", "10.0.0.123");
        if (!param.Exists("port")) param.Add("port", "502");
    }

    if (param["type"] == "InternalBool" || param["type"] == "InternalInt" || param["type"] == "InternalString")
    {
        if (!param.Exists("id")) param.Add("id", Calaos::get_new_id("intern_"));
    }
    else
    {
        if (!param.Exists("id")) param.Add("id", Calaos::get_new_id("input_"));
    }

    if (param["type"] == "InputTime" || param["type"] == "InputTimeDate" ||
        param["type"] == "InputTimer")
    {
        if (!param.Exists("hour")) param.Add("hour", "0");
        if (!param.Exists("min")) param.Add("min", "0");
        if (!param.Exists("sec")) param.Add("sec", "0");
    }
    if (param["type"] == "InputTimeDate")
    {
        if (!param.Exists("year")) param.Add("year", "0");
        if (!param.Exists("month")) param.Add("month", "0");
        if (!param.Exists("day")) param.Add("day", "0");
        param.Add("type", "InputTime");
    }

    if (param["type"] == "InputTimer")
    {
        if (!param.Exists("msec")) param.Add("msec", "0");
        std::string type = param["type"];
        input = IOFactory::CreateInput(type, param);
        if (input) room->AddInput(input);

        //also add the it as an output
        InputTimer *o = dynamic_cast<InputTimer *> (input);
        if (o) room->AddOutput(o);

        string sig = "new_input id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);

        //Also new output
        sig = "new_output id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);
    }
    else if (param["type"] == "scenario")
    {
        std::string type = param["type"];
        input = IOFactory::CreateInput(type, param);
        if (input) room->AddInput(input);

        //also add it as an output
        Scenario *o = dynamic_cast<Scenario *> (input);
        if (o) room->AddOutput(o);

        string sig = "new_input id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);

        //Also new output
        sig = "new_output id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);
    }
    else if (param["type"] == "InternalBool" || param["type"] == "InternalInt" || param["type"] == "InternalString")
    {
        if (!param.Exists("name")) param.Add("name", "Value");

        std::string type = param["type"];
        input = IOFactory::CreateInput(type, param);
        if (input) room->AddInput(input);

        //also add it as an output
        Internal *o = dynamic_cast<Internal *> (input);
        if (o) room->AddOutput(o);

        string sig = "new_input id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);

        //Also new output
        sig = "new_output id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);
    }
    else
    {
        std::string type = param["type"];
        input = IOFactory::CreateInput(type, param);
        if (input) room->AddInput(input);

        string sig = "new_input id:";
        sig += param["id"] + " ";
        sig += url_encode(string("room_name:") + room->get_name()) + " ";
        sig += url_encode(string("room_type:") + room->get_type());
        IPC::Instance().SendEvent("events", sig);
    }

    return input;
}

Output* ListeRoom::createOutput(Params param, Room *room)
{
    Output *output = NULL;

    if (!param.Exists("name")) param.Add("name", "Output");
    if (!param.Exists("type")) param.Add("type", "WODigital");
    if (!param.Exists("host")) param.Add("host", "10.0.0.123");
    if (!param.Exists("port")) param.Add("port", "502");
    if (param["type"] == "WOVolet")
    {
        if (!param.Exists("var_up")) param.Add("var_up", "0");
        if (!param.Exists("var_down")) param.Add("var_down", "0");
        if (!param.Exists("time")) param.Add("time", "30");
    }
    if (param["type"] == "WOVoletSmart")
    {
        if (!param.Exists("var_up")) param.Add("var_up", "0");
        if (!param.Exists("var_down")) param.Add("var_down", "0");
        if (!param.Exists("time_up")) param.Add("time_up", "30");
        if (!param.Exists("time_down")) param.Add("time_down", "28");
    }
    else if (param["type"] == "WONeon")
    {
        if (!param.Exists("var")) param.Add("var", "0");
        if (!param.Exists("var_relay")) param.Add("var_relay", "0");
    }
    else if (param["type"] == "WODali" && (param["type"] == "WODaliRVB"))
        if (!param.Exists("var")) param.Add("var", "0");
    if (!param.Exists("id")) param.Add("id", Calaos::get_new_id("output_"));

    std::string type = param["type"];
    output = IOFactory::CreateOutput(type, param);
    if (output) room->AddOutput(output);

    string sig = "new_output id:";
    sig += param["id"] + " ";
    sig += url_encode(string("room_name:") + room->get_name()) + " ";
    sig += url_encode(string("room_type:") + room->get_type());
    IPC::Instance().SendEvent("events", sig);

    return output;
}
