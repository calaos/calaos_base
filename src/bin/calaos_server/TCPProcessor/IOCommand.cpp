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
#include <IPCam.h>
#include <CamInput.h>
#include <CamManager.h>
#include <CamOutput.h>
#include <AudioInput.h>
#include <AudioOutput.h>
#include <AudioPlayer.h>
#include <AudioManager.h>
#include <IntValue.h>
#include <InPlageHoraire.h>
#include <ListeRule.h>
#include <InputTimer.h>
#include <WODigital.h>
#include <Scenario.h>
#include <Config.h>

using namespace Calaos;

void TCPConnection::IOCommand(Params &request, ProcessDone_cb callback)
{
        Params result = request;

        if (request["0"] == "input")
        {
                std::string id = request["1"];
                Input *input;

                input = ListeRoom::Instance().get_input(id);

                Utils::logger("network") << Priority::DEBUG << "TCPConnection::IOCommand(input)" << log4cpp::eol;
                if (input && request["2"] == "get")
                {
                        result.Add("1", "id:" + id);
                        result.Add("2", "name:" + input->get_param("name"));
                        result.Add("3", "type:" + input->get_param("type"));
                        string hits;
                        if (input->get_param("hits") == "")
                                hits = "0";
                        else
                                hits = input->get_param("hits");
                        result.Add("4", "hits:" + hits);
                        if (input->get_type() == TBOOL) result.Add("5", "var_type:bool");
                        if (input->get_type() == TINT) result.Add("5", "var_type:float");
                        if (input->get_type() == TSTRING) result.Add("5", "var_type:string");
                        result.Add("6", "visible:" + input->get_param("visible"));
                        if (input->get_param("chauffage_id") != "")
                                result.Add("7", "chauffage_id:" + input->get_param("chauffage_id"));

                        if(input->get_param("ioBoolState") != "")
                                result.Add("8", "ioBoolState:" + input->get_param("ioBoolState"));
                        if(input->get_param("auto_scenario") != "")
                                result.Add("9", "auto_scenario:" + input->get_param("auto_scenario"));

                        if(input->get_param("scenarioPref") != "")
                                result.Add("10", "scenarioPref:" + input->get_param("scenarioPref"));

                        if (input->get_param("rw") != "")
                                result.Add("11", "rw:" + input->get_param("rw"));

                        if (input->get_param("unit") != "")
                                result.Add("13", "unit:" + input->get_param("unit"));

                        result.Add("14", "gui_type:" + input->get_param("gui_type"));

                        switch (input->get_type())
                        {
                          case TINT:
                          {
                                result.Add("12", "state:" + Utils::to_string(input->get_value_double()));
                                break;
                          }
                          case TBOOL:
                          {
                                if (input->get_value_bool() == true)
                                {
                                        result.Add("12", "state:true");
                                        break;
                                }
                                else if (input->get_value_bool() == false)
                                {
                                        result.Add("12", "state:false");
                                        break;
                                }
                          }
                          case TSTRING:
                          {
                                result.Add("12", "state:" + input->get_value_string());
                                break;
                          }
                          default: break;
                        }
                }
                else if (input && request["2"] == "set")
                {
                        bool done = false;

                        switch (input->get_type())
                        {
                          case TINT:
                          {
                                if (Utils::is_of_type<double>(request["3"]))
                                {
                                        double value;
                                        Utils::from_string(request["3"], value);
                                        input->force_input_double(value);
                                        done = true;
                                }
                                break;
                          }
                          case TBOOL:
                          {
                                if (request["3"] == "true")
                                {
                                        input->force_input_bool(true);
                                        done = true;
                                }
                                else if (request["3"] == "false")
                                {
                                        input->force_input_bool(false);
                                        done = true;
                                }
                                break;
                          }
                          case TSTRING:
                          {
                                input->force_input_string(request["3"]);
                                done = true;
                                break;
                          }
                          default: break;
                        }

                        if (done)
                                result.Add("3", "ok");
                        else
                                result.Add("3", "error, incompatible type");
                }
                else if (input && request["2"] == "state?") //This returns the main status variable
                {
                        bool done = false;

                        switch (input->get_type())
                        {
                          case TINT:
                          {
                                double value = input->get_value_double();
                                result.Add("2", Utils::to_string(value));
                                done = true;
                                break;
                          }
                          case TBOOL:
                          {
                                if (input->get_value_bool())
                                {
                                        result.Add("2", "true");
                                        done = true;
                                }
                                else
                                {
                                        result.Add("2", "false");
                                        done = true;
                                }
                                break;
                          }
                          case TSTRING:
                          {
                                result.Add("2", input->get_value_string());
                                done = true;
                                break;
                          }
                          default: break;
                        }

                        if (!done)
                                result.Add("2", "error, incompatible type");
                }
                else if (input && request["2"] == "states?") //This returns all status variables available for that input
                {
                        bool done = false;

                        switch (input->get_type())
                        {
                          case TINT:
                          {
                                map<string, double> m = input->get_all_values_double();
                                map<string, double>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                        result.Add(Utils::to_string(cpt), (*it).first + ":" + Utils::to_string((*it).second));

                                done = true;
                                break;
                          }
                          case TBOOL:
                          {
                                map<string, bool> m = input->get_all_values_bool();
                                map<string, bool>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                {
                                        if ((*it).second)
                                                result.Add(Utils::to_string(cpt), (*it).first + ":true");
                                        else
                                                result.Add(Utils::to_string(cpt), (*it).first + ":false");
                                }

                                done = true;
                                break;
                          }
                          case TSTRING:
                          {
                                map<string, string> m = input->get_all_values_string();
                                map<string, string>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                        result.Add(Utils::to_string(cpt), (*it).first + ":" + (*it).second);

                                done = true;
                                break;
                          }
                          default: break;
                        }

                        if (!done)
                                result.Add("2", "error, incompatible type");
                }
                else if (input && request["2"] == "delete")
                {
                        bool done;
                        done = ListeRoom::Instance().deleteIO(input);
                        if (done)
                                result.Add("3", "ok");
                        else
                                result.Add("3", "error, not found");
                }
                else if (input && request["2"] == "query" && request["4"] == "?")
                {
                        string query_param = request["3"];

                        map<string, string> res = input->query_param(query_param);
                        map<string, string>::iterator it = res.begin();

                        for (int cpt = 4;it != res.end();it++, cpt++)
                                result.Add(Utils::to_string(cpt), (*it).first + ":" + (*it).second);
                }
                else if (input && request["2"] == "params?")
                {
                        int cpt = 1;

                        for (int i = 0;i < input->get_params().size();i++)
                        {
                                std::string key, val;
                                input->get_params().get_item(i, key, val);
                                key += ":" + val;
                                result.Add(Utils::to_string(cpt), key);
                                cpt++;
                        }
                }
                else if (input && request["2"] == "set_param")
                {
                        if (request["3"] != "" && request["4"] != "")
                        {
                                input->get_params().Add(request["3"], request["4"]);

                                {
                                        string sig = "input ";
                                        sig += input->get_param("id") + " ";
                                        sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }

                                if (request["3"] != "type" && request["3"] != "id")
                                {
                                        Internal *internal = dynamic_cast<Internal *> (input);
                                        if (internal)
                                        {
                                                string sig = "output ";
                                                sig += input->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        Scenario *scenario = dynamic_cast<Scenario *> (input);
                                        if (scenario)
                                        {
                                                string sig = "output ";
                                                sig += input->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        InputTimer *itimer = dynamic_cast<InputTimer *> (input);
                                        if (itimer)
                                        {
                                                string sig = "output ";
                                                sig += input->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        //broadcast value to parent
                                        CamInput *icam = dynamic_cast<CamInput *>(input);
                                        if (icam)
                                        {
                                                IPCam *cam = icam->get_cam();
                                                cam->get_params().Add(request["3"], request["4"]);
                                                cam->get_output()->get_params().Add(request["3"], request["4"]);

                                                string sig = "output ";
                                                sig += cam->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                        AudioInput *iaudio = dynamic_cast<AudioInput *>(input);
                                        if (iaudio)
                                        {
                                                AudioPlayer *audio = iaudio->get_player();
                                                audio->get_params().Add(request["3"], request["4"]);
                                                audio->get_output()->get_params().Add(request["3"], request["4"]);

                                                string sig = "output ";
                                                sig += audio->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                }

                                result.Add("4", "ok");
                        }
                }
                else if (input && request["2"] == "delete_param")
                {
                        if (request["3"] != "")
                        {
                                input->get_params().Delete(request["3"]);

                                {
                                        string sig = "input_delete_property ";
                                        sig += input->get_param("id") + " ";
                                        sig += url_encode(request["3"]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }

                                if (request["3"] != "type" && request["3"] != "id")
                                {
                                        //broadcast value to parent
                                        CamInput *icam = dynamic_cast<CamInput *>(input);
                                        if (icam)
                                        {
                                                IPCam *cam = icam->get_cam();
                                                cam->get_params().Delete(request["3"]);
                                                cam->get_output()->get_params().Delete(request["3"]);

                                                string sig = "output_delete_property ";
                                                sig += cam->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                        AudioInput *iaudio = dynamic_cast<AudioInput *>(input);
                                        if (iaudio)
                                        {
                                                AudioPlayer *audio = iaudio->get_player();
                                                audio->get_params().Delete(request["3"]);
                                                audio->get_output()->get_params().Delete(request["3"]);

                                                string sig = "output_delete_property ";
                                                sig += audio->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                }

                                result.Add("3", "ok");
                        }
                }
                else if (input && request["2"] == "plage") //PlageHoraire stuff
                {
                        InPlageHoraire *plage = dynamic_cast<InPlageHoraire *>(input);

                        //clean Params var
                        for (int i = 4;i < request.size();i++)
                                result.Add(Utils::to_string(i), "");

                        if (plage && request["3"] == "get")
                        {
                                int cpt = 3;
                                for (int day = 0;day < 7;day++)
                                {
                                        vector<TimeRange> h;
                                        if (day == 0) h = plage->getLundi();
                                        if (day == 1) h = plage->getMardi();
                                        if (day == 2) h = plage->getMercredi();
                                        if (day == 3) h = plage->getJeudi();
                                        if (day == 4) h = plage->getVendredi();
                                        if (day == 5) h = plage->getSamedi();
                                        if (day == 6) h = plage->getDimanche();
                                        for (uint i = 0;i < h.size();i++)
                                        {
                                                std::string s = Utils::to_string(day + 1) + ":";
                                                s += h[i].shour + ":" + h[i].smin + ":" + h[i].ssec;
                                                s += ":" + Utils::to_string(h[i].start_type);
                                                s += ":" + Utils::to_string(h[i].start_offset); //1 or -1
                                                s += ":" + h[i].ehour + ":" + h[i].emin + ":" + h[i].esec;
                                                s += ":" + Utils::to_string(h[i].end_type);
                                                s += ":" + Utils::to_string(h[i].end_offset); //1 or -1

                                                result.Add(Utils::to_string(cpt), s);
                                                cpt++;
                                        }
                                }
                        }
                        else if (plage && request["3"] == "set")
                        {
                                //1:12:00:00:0:1:14:00:00:0:1
                                plage->clear();

                                for (int i = 4;i < request.size();i++)
                                {
                                        vector<string> tokens;
                                        split(request[Utils::to_string(i)], tokens, ":", 11);

                                        vector<TimeRange> h;
                                        if (tokens[0] == "1") h = plage->getLundi();
                                        if (tokens[0] == "2") h = plage->getMardi();
                                        if (tokens[0] == "3") h = plage->getMercredi();
                                        if (tokens[0] == "4") h = plage->getJeudi();
                                        if (tokens[0] == "5") h = plage->getVendredi();
                                        if (tokens[0] == "6") h = plage->getSamedi();
                                        if (tokens[0] == "7") h = plage->getDimanche();

                                        TimeRange tr;
                                        tr.shour = tokens[1];
                                        tr.smin = tokens[2];
                                        tr.ssec = tokens[3];
                                        from_string(tokens[4], tr.start_type);
                                        from_string(tokens[5], tr.start_offset);
                                        if (tr.start_offset < 0) tr.start_offset = -1;
                                        if (tr.start_offset >= 0) tr.start_offset = 1;

                                        tr.ehour = tokens[6];
                                        tr.emin = tokens[7];
                                        tr.esec = tokens[8];
                                        from_string(tokens[9], tr.start_type);
                                        from_string(tokens[10], tr.start_offset);
                                        if (tr.start_offset < 0) tr.start_offset = -1;
                                        if (tr.start_offset >= 0) tr.start_offset = 1;

                                        h.push_back(tr);

                                        if (tokens[0] == "1") plage->setLundi(h);
                                        if (tokens[0] == "2") plage->setMardi(h);
                                        if (tokens[0] == "3") plage->setMercredi(h);
                                        if (tokens[0] == "4") plage->setJeudi(h);
                                        if (tokens[0] == "5") plage->setVendredi(h);
                                        if (tokens[0] == "6") plage->setSamedi(h);
                                        if (tokens[0] == "7") plage->setDimanche(h);

                                        request[Utils::to_string(i)] = "";
                                }

                                result.Add("4", "ok");

                                string sig = "input_range_change ";
                                sig += input->get_param("id") + " ";
                                IPC::Instance().SendEvent("events", sig);
                        }
                        else if (plage && request["3"] == "months" && request["4"] == "get")
                        {
                                stringstream ssmonth;
                                ssmonth << plage->months;
                                string str = ssmonth.str();
                                std::reverse(str.begin(), str.end());

                                result.Add("4", str);
                        }
                        else if (plage && request["3"] == "months" &&request["4"] == "set")
                        {
                                string m = request["5"];
                                //reverse to have a left to right months representation
                                std::reverse(m.begin(), m.end());

                                try
                                {
                                        bitset<12> mset(m);
                                        plage->months = mset;

                                        result.Add("5", "ok");
                                }
                                catch(...)
                                {
                                        Utils::logger("network") << Priority::ERROR << "TCPConnection::IOCommand(InPlageHoraire): wrong parameters for months: " << m << log4cpp::eol;

                                        result.Add("5", "error");
                                }
                        }
                        else
                                result.Add("4", "Error: not found");
                }
                else if (request["1"] == "list")
                {
                        for (int i = 0;i < ListeRoom::Instance().get_nb_input();i++)
                        {
                                Input *in = ListeRoom::Instance().get_input(i);
                                result.Add(Utils::to_string(i + 1), in->get_param("id"));
                        }
                }
        }
        else if (request["0"] == "output")
        {
                std::string id = request["1"];
                Output *output;

                output = ListeRoom::Instance().get_output(id);

                Utils::logger("network") << Priority::DEBUG << "TCPConnection::IOCommand(output)" << log4cpp::eol;
                if (output && request["2"] == "get")
                {
                        result.Add("1", "id:" + id);
                        result.Add("2", "name:" + output->get_param("name"));
                        result.Add("3", "type:" + output->get_param("type"));

                        switch (output->get_type())
                        {
                          case TINT:
                          {
                                result.Add("4", "state:" + Utils::to_string(output->get_value_double()));
                                break;
                          }
                          case TBOOL:
                          {
                                if (output->get_value_bool() == true)
                                {
                                        result.Add("4", "state:true");
                                        break;
                                }
                                else if (output->get_value_bool() == false)
                                {
                                        result.Add("4", "state:false");
                                        break;
                                }
                          }
                          case TSTRING:
                          {
                                result.Add("4", "state:" + output->get_value_string());
                                break;
                          }
                          default: break;
                        }

                        string hits;
                        if (output->get_param("hits") == "")
                                hits = "0";
                        else
                                hits = output->get_param("hits");
                        result.Add("5", "hits:" + hits);

                        if (output->get_type() == TBOOL) result.Add("6", "var_type:bool");
                        if (output->get_type() == TINT) result.Add("6", "var_type:float");
                        if (output->get_type() == TSTRING) result.Add("6", "var_type:string");

                        result.Add("7", "visible:" + output->get_param("visible"));

                        if (output->get_params().Exists("gtype"))
                                result.Add("8", "gtype:" + output->get_param("gtype"));

                        if (output->get_param("chauffage_id") != "")
                                result.Add("9", "chauffage_id:" + output->get_param("chauffage_id"));

                        if (output->get_param("rw") != "")
                                result.Add("10", "rw:" + output->get_param("rw"));

                        if(output->get_param("ioBoolState") != "")
                                result.Add("11", "ioBoolState:" + output->get_param("ioBoolState"));

                        if(output->get_param("auto_scenario") != "")
                                result.Add("12", "auto_scenario:" + output->get_param("auto_scenario"));

                        if(output->get_param("unit") != "")
                                result.Add("13", "unit:" + output->get_param("unit"));

                        if(output->get_param("scenarioPref") != "")
                                result.Add("14", "scenarioPref:" + output->get_param("scenarioPref"));

                        result.Add("15", "gui_type:" + output->get_param("gui_type"));

                        if(output->get_param("step") != "")
                          result.Add("16", "step:" + output->get_param("step"));

                }
                else if (output && request["2"] == "set")
                {
                        bool done = false;

                        switch (output->get_type())
                        {
                          case TINT:
                          {
                                if (Utils::is_of_type<double>(request["3"]))
                                {
                                        double value;
                                        Utils::from_string(request["3"], value);
                                        output->set_value(value);
                                        done = true;
                                }
                                else
                                {
                                        output->set_value(request["3"]);
                                        done = true;
                                }
                                break;
                          }
                          case TBOOL:
                          {
                                if (request["3"] == "true")
                                {
                                        output->set_value(true);
                                        done = true;
                                }
                                else if (request["3"] == "false")
                                {
                                        output->set_value(false);
                                        done = true;
                                }
                                else
                                {
                                        output->set_value(request["3"]);
                                        done = true;
                                }
                                break;
                          }
                          case TSTRING:
                          {
                                output->set_value(request["3"]);
                                done = true;
                                break;
                          }
                          default: break;
                        }

                        if (done)
                                result.Add("3", "ok");
                        else
                                result.Add("3", "error, incompatible type");
                }
                else if (output && request["2"] == "state?")
                {
                        switch (output->get_type())
                        {
                          case TINT:
                          {
                                result.Add("2", Utils::to_string(output->get_value_double()));
                                break;
                          }
                          case TBOOL:
                          {
                                if (output->get_value_bool() == true)
                                {
                                        result.Add("2", "true");
                                        break;
                                }
                                else if (output->get_value_bool() == false)
                                {
                                        result.Add("2", "false");
                                        break;
                                }
                          }
                          case TSTRING:
                          {
                                result.Add("2", output->get_value_string());
                                break;
                          }
                          default: break;
                        }
                }
                else if (output && request["2"] == "states?") //This returns all status variables available for that input
                {
                        bool done = false;

                        switch (output->get_type())
                        {
                          case TINT:
                          {
                                map<string, double> m = output->get_all_values_double();
                                map<string, double>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                        result.Add(Utils::to_string(cpt), (*it).first + ":" + Utils::to_string((*it).second));

                                done = true;
                                break;
                          }
                          case TBOOL:
                          {
                                map<string, bool> m = output->get_all_values_bool();
                                map<string, bool>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                {
                                        if ((*it).second)
                                                result.Add(Utils::to_string(cpt), (*it).first + ":true");
                                        else
                                                result.Add(Utils::to_string(cpt), (*it).first + ":false");
                                }

                                done = true;
                                break;
                          }
                          case TSTRING:
                          {
                                map<string, string> m = output->get_all_values_string();
                                map<string, string>::iterator it = m.begin();

                                int cpt = 2;
                                for (;it != m.end();it++, cpt++)
                                        result.Add(Utils::to_string(cpt), (*it).first + ":" + (*it).second);

                                done = true;
                                break;
                          }
                          default: break;
                        }

                        if (!done)
                                result.Add("2", "error, incompatible type");
                }
                else if (output && request["2"] == "delete")
                {
                        bool done = ListeRoom::Instance().deleteIO(output);
                        if (done)
                                result.Add("3", "ok");
                        else
                                result.Add("3", "error, not found");
                }
                else if (output && request["2"] == "query" && request["4"] == "?")
                {
                        string query_param = request["3"];

                        map<string, string> res = output->query_param(query_param);
                        map<string, string>::iterator it = res.begin();

                        for (int cpt = 4;it != res.end();it++, cpt++)
                                result.Add(Utils::to_string(cpt), (*it).first + ":" + (*it).second);
                }
                else if (output && request["2"] == "params?")
                {
                        int cpt = 1;

                        for (int i = 0;i < output->get_params().size();i++)
                        {
                                std::string key, val;
                                output->get_params().get_item(i, key, val);
                                key += ":" + val;
                                result.Add(Utils::to_string(cpt), key);
                                cpt++;
                        }
                }
                else if (output && request["2"] == "set_param")
                {
                        if (request["3"] != "" && request["4"] != "")
                        {
                                output->get_params().Add(request["3"], request["4"]);

                                {
                                        string sig = "output ";
                                        sig += output->get_param("id") + " ";
                                        sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }

                                if (request["3"] != "type" && request["3"] != "id")
                                {
                                        Internal *internal = dynamic_cast<Internal *> (output);
                                        if (internal)
                                        {
                                                string sig = "input ";
                                                sig += output->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        Scenario *scenario = dynamic_cast<Scenario *> (output);
                                        if (scenario)
                                        {
                                                string sig = "input ";
                                                sig += output->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        InputTimer *itimer = dynamic_cast<InputTimer *> (output);
                                        if (itimer)
                                        {
                                                string sig = "input ";
                                                sig += output->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }

                                        //broadcast value to parent
                                        CamOutput *icam = dynamic_cast<CamOutput *>(output);
                                        if (icam)
                                        {
                                                IPCam *cam = icam->get_cam();
                                                cam->get_params().Add(request["3"], request["4"]);
                                                cam->get_input()->get_params().Add(request["3"], request["4"]);

                                                string sig = "input ";
                                                sig += cam->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                        AudioOutput *iaudio = dynamic_cast<AudioOutput *>(output);
                                        if (iaudio)
                                        {
                                                AudioPlayer *audio = iaudio->get_player();
                                                audio->get_params().Add(request["3"], request["4"]);
                                                audio->get_input()->get_params().Add(request["3"], request["4"]);

                                                string sig = "input ";
                                                sig += audio->get_output()->get_param("id") + " ";
                                                sig += url_encode(request["3"] + string(":") + request["4"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                }
                                result.Add("4", "ok");
                        }
                }
                else if (output && request["2"] == "delete_param")
                {
                        if (request["3"] != "")
                        {
                                output->get_params().Delete(request["3"]);

                                {
                                        string sig = "output_delete_property ";
                                        sig += output->get_param("id") + " ";
                                        sig += url_encode(request["3"]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }

                                if (request["3"] != "type" && request["3"] != "id")
                                {
                                        //broadcast value to parent
                                        CamOutput *icam = dynamic_cast<CamOutput *>(output);
                                        if (icam)
                                        {
                                                IPCam *cam = icam->get_cam();
                                                cam->get_params().Delete(request["3"]);
                                                cam->get_input()->get_params().Delete(request["3"]);

                                                string sig = "input_delete_property ";
                                                sig += cam->get_input()->get_param("id") + " ";
                                                sig += url_encode(request["3"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                        AudioOutput *iaudio = dynamic_cast<AudioOutput *>(output);
                                        if (iaudio)
                                        {
                                                AudioPlayer *audio = iaudio->get_player();
                                                audio->get_params().Delete(request["3"]);
                                                audio->get_input()->get_params().Delete(request["3"]);

                                                string sig = "input_delete_property ";
                                                sig += audio->get_input()->get_param("id") + " ";
                                                sig += url_encode(request["3"]) + " ";
                                                IPC::Instance().SendEvent("events", sig);
                                        }
                                }

                                result.Add("3", "ok");
                        }
                }
                else if (request["1"] == "list")
                {
                        for (int i = 0;i < ListeRoom::Instance().get_nb_output();i++)
                        {
                                Output *o = ListeRoom::Instance().get_output(i);
                                result.Add(Utils::to_string(i + 1), o->get_param("id"));
                        }
                }
        }
        else if (request["0"] == "io" && request["1"] == "scenarios_pref")
        {
                for( int i = 2; i<request.size(); i++)
                {
                        vector<string> sSplit;
                        split(request[Utils::to_string(i)], sSplit,":");

                        Output *output = ListeRoom::Instance().get_output(sSplit[0]);
                        if(output)
                        {
                                output->set_param("scenarioPref",sSplit[1]);
                                string sig = "output ";
                                sig += output->get_param("id") + " ";
                                sig += url_encode("scenarioPref" + string(":") + sSplit[1]) + " ";
                                IPC::Instance().SendEvent("events", sig);
                        }
                        else
                        {
                                Input *input = ListeRoom::Instance().get_input(sSplit[0]);
                                if(input)
                                {
                                        input->set_param("scenarioPref",sSplit[1]);
                                        string sig = "input ";
                                        sig += input->get_param("id") + " ";
                                        sig += url_encode("ioPref" + string(":") + sSplit[1]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }
                        }
                }
                Config::Instance().SaveConfigIO();
        }
        else if (request["0"] == "io" && request["1"] == "hits_change")
        {
                for( int i = 2; i<request.size(); i++)
                {
                        vector<string> sSplit;
                        split(request[Utils::to_string(i)], sSplit,":");

                        Output *output = ListeRoom::Instance().get_output(sSplit[0]);
                        if(output)
                        {
                                output->set_param("hits",sSplit[1]);
                                string sig = "output ";
                                sig += output->get_param("id") + " ";
                                sig += url_encode("hits" + string(":") + sSplit[1]) + " ";
                                IPC::Instance().SendEvent("events", sig);
                        }
                        else
                        {
                                Input *input = ListeRoom::Instance().get_input(sSplit[0]);
                                if(input)
                                {
                                        input->set_param("hits",sSplit[1]);
                                        string sig = "input ";
                                        sig += input->get_param("id") + " ";
                                        sig += url_encode("hits" + string(":") + sSplit[1]) + " ";
                                        IPC::Instance().SendEvent("events", sig);
                                }
                        }
                }
                Config::Instance().SaveConfigIO();
        }

        //return the result
        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);
}
