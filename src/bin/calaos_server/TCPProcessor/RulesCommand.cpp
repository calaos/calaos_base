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
#include <ListeRule.h>
#include <Rule.h>
#include <ConditionStd.h>
#include <ActionStd.h>

using namespace CalaosNetwork;
using namespace Calaos;

void TCPConnection::RulesCommand(Params &request, ProcessDone_cb callback)
{
        Params result = request;

        if (request["0"] == "rules")
        {
                Utils::logger("network") << Priority::DEBUG << "TCPConnection::RulesCommand(rules)" << log4cpp::eol;
                if (request["1"] == "?")
                {
                        map<std::string, int> rtype;
                        for (int i = 0;i < ListeRule::Instance().size();i++)
                        {
                                Rule *rule = ListeRule::Instance().get_rule(i);
                                if (!rule) continue;
                                map<std::string, int>::iterator fter = rtype.find(rule->get_type());
                                if (fter != rtype.end())
                                        rtype[rule->get_type()]++;
                                else
                                        rtype[rule->get_type()] = 1;
                        }

                        for (uint i = 0;i < rtype.size();i++)
                        {
                                int num = i;
                                std::string key;
                                int value;
                                map<std::string, int>::iterator iter;
                                for (iter = rtype.begin();iter != rtype.end();iter++,num--)
                                {
                                        if (num == 0)
                                        {
                                                key = (*iter).first;
                                                value = (*iter).second;
                                                break;
                                        }
                                }

                                result.Add(Utils::to_string(i + 1), key + ":" + Utils::to_string(value));
                        }
                }
                else if (request["1"] == "add" && request["4"] != "condition" && request["4"] != "action")
                {
                        //clean Params var
                        for (int i = 2;i < request.size();i++)
                                result.Add(Utils::to_string(i), "");

                        std::string type = request["2"];
                        std::string name = request["3"];
                        bool exist = false;

                        for (int i = 0;i < ListeRule::Instance().size();i++)
                        {
                                if (ListeRule::Instance().get_rule(i)->get_type() == type &&
                                    ListeRule::Instance().get_rule(i)->get_name() == name)
                                        exist = true;
                        }

                        if (!exist)
                        {
                                Rule *rule = new Rule(type, name);
                                ListeRule::Instance().Add(rule);

                                for (int i = 4;i < request.size();i++)
                                {
                                        std::string p = request[Utils::to_string(i)];
                                        vector<string> splitter;
                                        Utils::split(p, splitter, ":", 3);

                                        int num;
                                        Utils::from_string(splitter[0], num);

                                        if (splitter[1] == "iid" || splitter[1] == "ioper" || splitter[1] == "ival" || splitter[1] == "ivar_val")
                                        {
                                                if (rule->get_size_conds() < num + 1)
                                                        rule->AddCondition(new ConditionStd());

                                                ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(num));
                                                if (!cond) continue;
                                                if (splitter[1] == "iid")
                                                {
                                                        Input *in = ListeRoom::Instance().get_input(splitter[2]);
                                                        if (in) cond->Add(in);
                                                }
                                                else if (splitter[1] == "ioper" && cond->get_size() > 0)
                                                        cond->get_operator().Add(cond->get_input(0)->get_param("id"), splitter[2]);
                                                else if (splitter[1] == "ival" && cond->get_size() > 0)
                                                        cond->get_params().Add(cond->get_input(0)->get_param("id"), splitter[2]);
                                                else if (splitter[1] == "ivar_val" && cond->get_size() > 0)
                                                        cond->get_params_var().Add(cond->get_input(0)->get_param("id"), splitter[2]);
                                        }
                                        else if (splitter[1] == "oid" || splitter[1] == "oval" || splitter[1] == "ovar_val")
                                        {
                                                if (rule->get_size_actions() < num + 1)
                                                        rule->AddAction(new ActionStd());

                                                ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(num));
                                                if (!action) continue;
                                                if (splitter[1] == "oid")
                                                {
                                                        Output *out = ListeRoom::Instance().get_output(splitter[2]);
                                                        if (out) action->Add(out);
                                                }
                                                else if (splitter[1] == "oval" && action->get_size() > 0)
                                                        action->get_params().Add(action->get_output(0)->get_param("id"), splitter[2]);
                                                else if (splitter[1] == "ovar_val" && action->get_size() > 0)
                                                        action->get_params_var().Add(action->get_output(0)->get_param("id"), splitter[2]);
                                        }
                                }

                                result.Add("2", "ok");
                        }
                        else
                                result.Add("2", "rule exists");
                }
                else
                {
                        vector<Rule *> list;

                        for (int i = 0;i < ListeRule::Instance().size();i++)
                        {
                                Rule *rule = ListeRule::Instance().get_rule(i);
                                if (rule->get_type() == request["1"])
                                        list.push_back(rule);
                        }

                        int num;
                        Utils::from_string(request["3"], num);

                        if (num < 0 || num >= (int)list.size())
                        {
                                //clean Params var
                                for (int i = 2;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                result.Add("2", "Error: out of bound");
                        }
                        else if (request["2"] == "delete" && request.size() == 4)
                        {
                                //clean Params var
                                for (int i = 2;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                Rule *rule = list[num];
                                ListeRule::Instance().Remove(rule);

                                result.Add("2", "ok");
                        }
                        else if (request["2"] == "get" && request["4"] == "condition")
                        {
                                //clean Params var
                                for (int i = 2;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                Rule *rule = list[num];
                                result.Add("1", "type:" + rule->get_type());
                                result.Add("2", "name:" + rule->get_name());

                                int cpt = 3;
                                for (int i = 0;i < rule->get_size_conds();i++)
                                {
                                        Condition *_cond = rule->get_condition(i);

                                        switch (_cond->getType())
                                        {
                                        case COND_STD:
                                        {
                                                ConditionStd *cond = dynamic_cast<ConditionStd *>(_cond);
                                                if (cond && cond->get_size() > 0)
                                                {
                                                        std::string id = cond->get_input(0)->get_param("id");
                                                        result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":id:" + id);
                                                        cpt++;
                                                        result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":oper:" + cond->get_operator().get_param(id));
                                                        cpt++;
                                                        result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":val:" + cond->get_params().get_param(id));
                                                        cpt++;
                                                        if (cond->get_params_var().Exists(id) && cond->get_params_var().get_param(id) != "")
                                                        {
                                                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":var_val:" + cond->get_params_var().get_param(id));
                                                                cpt++;
                                                        }
                                                }
                                        }
                                                break;

                                        default:
                                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":condition:unknown");
                                                cpt++;
                                        }
                                }
                        }
                        else if (request["2"] == "set" && request["4"] == "condition")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 5;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                if (cnum < 0 || cnum >= rule->get_size_conds())
                                {
                                        result.Add("5", "Error: out of bound");
                                }
                                else
                                {
                                        ConditionStd *cond = dynamic_cast<ConditionStd *>(rule->get_condition(cnum));
                                        if (!cond)
                                        {
                                                result.Add("5", "failed");
                                        }
                                        else
                                        {
                                                for (int i = 6;i < request.size();i++)
                                                {
                                                        std::string val = request[Utils::to_string(i)];
                                                        vector<string> splitter;
                                                        Utils::split(val, splitter, ":", 2);

                                                        if (cond->get_size() > 0)
                                                        {
                                                                if (splitter[0] == "oper" && cond->get_size() > 0)
                                                                        cond->get_operator().Add(cond->get_input(0)->get_param("id"), splitter[1]);
                                                                if (splitter[0] == "val" && cond->get_size() > 0)
                                                                        cond->get_params().Add(cond->get_input(0)->get_param("id"), splitter[1]);
                                                                if (splitter[0] == "var_val" && cond->get_size() > 0)
                                                                        cond->get_params_var().Add(cond->get_input(0)->get_param("id"), splitter[1]);
                                                                if (splitter[0] == "id" && cond->get_size() > 0)
                                                                {
                                                                        Input *in = ListeRoom::Instance().get_input(splitter[1]);
                                                                        if (in)
                                                                        {
                                                                                cond->Remove(0);
                                                                                cond->Add(in);
                                                                        }
                                                                }

                                                                result.Add("5", "ok");
                                                        }
                                                }
                                        }
                                }
                        }
                        else if (request["2"] == "add" && request["4"] == "condition")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 3;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                ConditionStd *cond = new ConditionStd();
                                rule->AddCondition(cond);
                                for (int i = 5;i < request.size();i++)
                                {
                                        std::string val = request[Utils::to_string(i)];
                                        vector<string> splitter;
                                        Utils::split(val, splitter, ":", 2);

                                        if (splitter[0] == "id")
                                        {
                                                Input *in = ListeRoom::Instance().get_input(splitter[1]);
                                                if (in) cond->Add(in);
                                        }

                                        if (cond->get_size() > 0)
                                        {
                                                if (splitter[0] == "oper" && cond->get_size() > 0)
                                                        cond->get_operator().Add(cond->get_input(0)->get_param("id"), splitter[1]);
                                                if (splitter[0] == "val" && cond->get_size() > 0)
                                                        cond->get_params().Add(cond->get_input(0)->get_param("id"), splitter[1]);
                                                if (splitter[0] == "var_val" && cond->get_size() > 0)
                                                        cond->get_params_var().Add(cond->get_input(0)->get_param("id"), splitter[1]);

                                                result.Add("3", "ok");
                                        }
                                }
                        }
                        else if (request["2"] == "delete" && request["4"] == "condition")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 5;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                if (cnum < 0 || cnum >= rule->get_size_conds())
                                {
                                        result.Add("5", "Error: out of bound");
                                }
                                else
                                {
                                        rule->RemoveCondition(cnum);
                                        result.Add("5", "ok");
                                }
                        }
                        else if (request["2"] == "get" && request["4"] == "action")
                        {
                                //clean Params var
                                for (int i = 2;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                Rule *rule = list[num];
                                result.Add("1", "type:" + rule->get_type());
                                result.Add("2", "name:" + rule->get_name());

                                int cpt = 3;
                                for (int i = 0;i < rule->get_size_actions();i++)
                                {
                                        Action *_action = rule->get_action(i);

                                        switch (_action->getType())
                                        {
                                        case ACTION_STD:
                                        {
                                                ActionStd *action = dynamic_cast<ActionStd *>(_action);

                                                if (action->get_size() > 0)
                                                {
                                                        std::string id = action->get_output(0)->get_param("id");
                                                        result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":id:" + id);
                                                        cpt++;
                                                        result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":val:" + action->get_params().get_param(id));
                                                        cpt++;
                                                        if (action->get_params_var().Exists(id) && action->get_params_var().get_param(id) != "")
                                                        {
                                                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":var_val:" + action->get_params_var().get_param(id));
                                                                cpt++;
                                                        }
                                                }
                                        }
                                                break;

                                        default:
                                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":action:unkown");
                                                cpt++;
                                        }
                                }
                        }
                        else if (request["2"] == "set" && request["4"] == "action")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 5;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                if (cnum < 0 || cnum >= rule->get_size_actions())
                                {
                                        result.Add("5", "Error: out of bound");
                                }
                                else
                                {
                                        ActionStd *action = dynamic_cast<ActionStd *>(rule->get_action(cnum));
                                        if (!action)
                                        {
                                                result.Add("5", "failed");
                                        }
                                        else
                                        {
                                                for (int i = 6;i < request.size();i++)
                                                {
                                                        std::string val = request[Utils::to_string(i)];
                                                        vector<string> splitter;
                                                        Utils::split(val, splitter, ":", 2);

                                                        if (action->get_size() > 0)
                                                        {
                                                                if (splitter[0] == "val" && action->get_size() > 0)
                                                                        action->get_params().Add(action->get_output(0)->get_param("id"), splitter[1]);
                                                                if (splitter[0] == "var_val" && action->get_size() > 0)
                                                                        action->get_params_var().Add(action->get_output(0)->get_param("id"), splitter[1]);
                                                                if (splitter[0] == "id")
                                                                {
                                                                        Output *out = ListeRoom::Instance().get_output(splitter[1]);
                                                                        if (out)
                                                                        {
                                                                                action->Remove(0);
                                                                                action->Add(out);
                                                                        }
                                                                }

                                                                result.Add("5", "ok");
                                                        }
                                                }
                                        }
                                }
                        }
                        else if (request["2"] == "delete" && request["4"] == "action")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 5;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                if (cnum < 0 || cnum >= rule->get_size_actions())
                                {
                                        result.Add("5", "Error: out of bound");
                                }
                                else
                                {
                                        rule->RemoveAction(cnum);
                                        result.Add("5", "ok");
                                }
                        }
                        else if (request["2"] == "add" && request["4"] == "action")
                        {
                                Rule *rule = list[num];

                                int cnum;
                                Utils::from_string(request["5"], cnum);

                                //clean Params var
                                for (int i = 3;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                                ActionStd *action = new ActionStd();
                                rule->AddAction(action);
                                for (int i = 5;i < request.size();i++)
                                {
                                        std::string val = request[Utils::to_string(i)];
                                        vector<string> splitter;
                                        Utils::split(val, splitter, ":", 2);

                                        if (splitter[0] == "id")
                                        {
                                                Output *out = ListeRoom::Instance().get_output(splitter[1]);
                                                if (out) action->Add(out);
                                        }

                                        if (action->get_size() > 0)
                                        {
                                                if (splitter[0] == "val" && action->get_size() > 0)
                                                        action->get_params().Add(action->get_output(0)->get_param("id"), splitter[1]);
                                                if (splitter[0] == "var_val" && action->get_size() > 0)
                                                        action->get_params_var().Add(action->get_output(0)->get_param("id"), splitter[1]);

                                                result.Add("3", "ok");
                                        }
                                }
                        }
                }
        }

        //return the result
        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);
}
