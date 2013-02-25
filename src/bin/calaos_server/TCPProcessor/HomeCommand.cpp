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
#include <IOFactory.h>
#include <CamManager.h>
#include <IPCam.h>
#include <AudioManager.h>
#include <AudioPlayer.h>
#include <IntValue.h>
#include <InputTimer.h>
#include <Scenario.h>

using namespace CalaosNetwork;
using namespace Calaos;

void TCPConnection::HomeCommand(Params &request, ProcessDone_cb callback)
{
        Params result = request;

        if (request["0"] == "home")
        {
                Utils::logger("network") << Priority::DEBUG << "TCPConnection::HomeCommand(home)" << log4cpp::eol;
                if (request["1"] == "?")
                {
                        std::map<std::string, int> list;
                        for (int i = 0;i < ListeRoom::Instance().size();i++)
                        {
                                Room *room = ListeRoom::Instance().get_room(i);

                                map<std::string, int>::iterator fter = list.find(room->get_type());
                                if (fter != list.end())
                                        list[room->get_type()] = list[room->get_type()]++;
                                else
                                        list[room->get_type()] = 1;
                        }

                        map<std::string, int>::iterator fter = list.begin();
                        for (int cpt = 1;fter != list.end();fter++, cpt++)
                                result.Add(Utils::to_string(cpt), fter->first + ":" + Utils::to_string(fter->second));
                }
                else if (request["1"] == "get")
                {
                        std::map<std::string, int> list;
                        vector<Room *> rooms;
                        for (int i = 0;i < ListeRoom::Instance().size();i++)
                        {
                                Room *room = ListeRoom::Instance().get_room(i);

                                if (room->get_type() == request["2"])
                                        rooms.push_back(room);
                        }

                        result.Add("1", "count:" + Utils::to_string(rooms.size()));
                        int cpt = 2;
                        for (int i = 0;i < rooms.size();i++)
                        {
                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":name:" + rooms[i]->get_name());
                                cpt++;
                                result.Add(Utils::to_string(cpt), Utils::to_string(i) + ":hits:" + Utils::to_string(rooms[i]->get_hits()));
                                cpt++;
                        }
                }
        }
        else if (request["0"] == "room")
        {
                string room_type = request["1"];
                bool found = false;

                //search for the requested room type
                for (int i = 0;i < ListeRoom::Instance().size() && !found;i++)
                {
                        Room *room = ListeRoom::Instance().get_room(i);

                        if (room->get_type() == room_type)
                                found = true;
                }

                Utils::logger("network") << Priority::DEBUG << "TCPConnection::HomeCommand(room)" << log4cpp::eol;
                if (found && request["2"] == "?")
                {
                        vector<Room *> rooms;
                        for (int i = 0;i < ListeRoom::Instance().size();i++)
                        {
                                Room *room = ListeRoom::Instance().get_room(i);

                                if (room->get_type() == room_type)
                                        rooms.push_back(room);
                        }

                        result.Add("1", room_type + ":" + Utils::to_string(rooms.size()));
                        result.Add("2", "");
                }
                else if (found && request["2"] == "get")
                {
                        if (Utils::is_of_type<int>(request["3"]))
                        {
                                int id;
                                Utils::from_string(request["3"], id);
                                vector<IOBase *> ins, outs;

                                vector<Room *> rooms;
                                for (int i = 0;i < ListeRoom::Instance().size();i++)
                                {
                                        Room *room = ListeRoom::Instance().get_room(i);

                                        if (room->get_type() == room_type)
                                                rooms.push_back(room);
                                }
                                if (id >= 0 && id < rooms.size())
                                {
                                        Room *room = rooms[id];

                                        for (int i = 0;i < room->get_size_in();i++)
                                                ins.push_back(room->get_input(i));
                                        for (int i = 0;i < room->get_size_out();i++)
                                                outs.push_back(room->get_output(i));

                                        result.Add("1", room_type + ":" + Utils::to_string(id));
                                        result.Add("2","name:"+rooms[id]->get_name());
                                        result.Add("3","hits:"+Utils::to_string(rooms[id]->get_hits()));
                                }
                                else
                                {
                                        result.Add("1", "Error_Out_Of_Bound");
                                }

                                int cpt = 4;
                                for (int i = 0;i < ins.size();i++)
                                {
                                        Input *in = dynamic_cast<Input *>(ins[i]);
                                        if (in)
                                        {
                                                result.Add(Utils::to_string(cpt), "input:" + in->get_param("id"));
                                                cpt++;
                                        }
                                }

                                for (int i = 0;i < outs.size();i++)
                                {
                                        Output *out = dynamic_cast<Output *>(outs[i]);
                                        if (out)
                                        {
                                                result.Add(Utils::to_string(cpt), "output:" + out->get_param("id"));
                                                cpt++;
                                        }
                                }

                                //clean Params var
                                for (int i = cpt;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");
                        }
                        else //request[3] is the name of the room and not the ID
                        {
                                int id;
                                string name = request["3"];
                                vector<IOBase *> ins, outs;
                                Room* room = NULL;

                                for (int i = 0;i < ListeRoom::Instance().size();i++)
                                {
                                        Room *_room = ListeRoom::Instance().get_room(i);
                                        if (_room->get_name() == name
                                                        && _room->get_type() == room_type)
                                                room = _room;
                                }
                                if(room)
                                {
                                        for (int i = 0;i < room->get_size_in();i++)
                                                ins.push_back(room->get_input(i));
                                        for (int i = 0;i < room->get_size_out();i++)
                                                outs.push_back(room->get_output(i));

                                        result.Add("1", room_type + ":" + Utils::to_string(id));
                                        result.Add("2","name:"+room->get_name());
                                        result.Add("3","hits:"+Utils::to_string(room->get_hits()));
                                }

                                int cpt = 4;
                                for (int i = 0;i < ins.size();i++)
                                {
                                        Input *in = dynamic_cast<Input *>(ins[i]);
                                        if (in)
                                        {
                                                result.Add(Utils::to_string(cpt), "input:" + in->get_param("id"));
                                                cpt++;
                                        }
                                }

                                for (int i = 0;i < outs.size();i++)
                                {
                                        Output *out = dynamic_cast<Output *>(outs[i]);
                                        if (out)
                                        {
                                                result.Add(Utils::to_string(cpt), "output:" + out->get_param("id"));
                                                cpt++;
                                        }
                                }

                                //clean Params var
                                for (int i = cpt;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");

                        }
                }
                else if (found && request["2"] == "set")
                {
                        if (Utils::is_of_type<int>(request["3"]))
                        {
                                int id;
                                Utils::from_string(request["3"], id);
                                std::string option = request["4"];
                                std::string value = request["5"];

                                vector<Room *> rooms;
                                for (int i = 0;i < ListeRoom::Instance().size();i++)
                                {
                                        Room *room = ListeRoom::Instance().get_room(i);

                                        if (room->get_type() == room_type)
                                                rooms.push_back(room);
                                }

                                if (id >= 0 && id < rooms.size())
                                {
                                        Room *room = rooms[id];
                                        if (option == "name")
                                                room->set_name(value);
                                        else if (option == "type")
                                                room->set_type(value);
                                        else if (option == "hits")
                                        {
                                                int h;
                                                if (Utils::is_of_type<int>(value))
                                                {
                                                        Utils::from_string(value, h);
                                                        room->set_hits(h);
                                                }
                                        }

                                        result.Add("5", "ok");
                                }
                        }
                }
                else if (request["1"] == "get")
                {
                        if (Utils::is_of_type<int>(request["2"]))
                        {
                                int id;
                                Utils::from_string(request["2"], id);
                                vector<IOBase *> ins, outs;

                                if (id >= 0 && id < ListeRoom::Instance().size())
                                {
                                        Room *room = ListeRoom::Instance().get_room(id);

                                        for (int i = 0;i < room->get_size_in();i++)
                                                ins.push_back(room->get_input(i));
                                        for (int i = 0;i < room->get_size_out();i++)
                                                outs.push_back(room->get_output(i));
                                }

                                result.Add("1", "id:" + Utils::to_string(id));
                                int cpt = 2;
                                for (int i = 0;i < ins.size();i++)
                                {
                                        Input *in = dynamic_cast<Input *>(ins[i]);
                                        if (in)
                                        {
                                                result.Add(Utils::to_string(cpt), "input:" + in->get_param("id"));
                                                cpt++;
                                        }
                                }

                                for (int i = 0;i < outs.size();i++)
                                {
                                        Output *out = dynamic_cast<Output *>(outs[i]);
                                        if (out)
                                        {
                                                result.Add(Utils::to_string(cpt), "output:" + out->get_param("id"));
                                                cpt++;
                                        }
                                }

                                //clean Params var
                                for (int i = cpt;i < request.size();i++)
                                        result.Add(Utils::to_string(i), "");
                        }
                }
                else if (found && request["2"] == "delete")
                {
                        if (Utils::is_of_type<int>(request["3"]))
                        {
                                int id;
                                Utils::from_string(request["3"], id);

                                vector<Room *> rooms;
                                for (int i = 0;i < ListeRoom::Instance().size();i++)
                                {
                                        Room *room = ListeRoom::Instance().get_room(i);

                                        if (room->get_type() == room_type)
                                                rooms.push_back(room);
                                }
                                if (id >= 0 && id < rooms.size())
                                {
                                        Room *room = rooms[id];
                                        int ii = -1;

                                        for (int i = 0;i < ListeRoom::Instance().size();i++)
                                        {
                                                if (ListeRoom::Instance().get_room(i) == room)
                                                        ii = i;
                                        }

                                        string sig = "delete_room ";
                                        sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                        sig += url_encode(string("room_type:") + room->get_type());
                                        IPC::Instance().SendEvent("events", sig);

                                        ListeRoom::Instance().Remove(ii);

                                        result.Add("3", "ok");
                                }
                        }
                }
                else if (request["1"] == "add")
                {
                        if (request["2"] != "" && request["3"] != "")
                        {
                                Room *room = new Room(request["3"], request["2"], 0);
                                ListeRoom::Instance().Add(room);

                                string sig = "new_room ";
                                sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                sig += url_encode(string("room_type:") + room->get_type());
                                IPC::Instance().SendEvent("events", sig);

                                result["2"] = "ok";
                                result["3"] = "";
                        }
                }
                else if (found && request["3"] == "create")
                {
                        if (Utils::is_of_type<int>(request["2"]))
                        {
                                int id;
                                Utils::from_string(request["2"], id);

                                vector<Room *> rooms;
                                for (int i = 0;i < ListeRoom::Instance().size();i++)
                                {
                                        Room *room = ListeRoom::Instance().get_room(i);

                                        if (room->get_type() == room_type)
                                                rooms.push_back(room);
                                }
                                if (id >= 0 && id < rooms.size())
                                {
                                        Room *room = rooms[id];
                                        Params param;

                                        for (int i = 0;i < request.size();i++)
                                        {
                                                std::string key, val;
                                                request.get_item(i, key, val);
                                                int k;
                                                Utils::from_string(key, k);
                                                if (k >= 5)
                                                {
                                                        vector<string> s;
                                                        Utils::split(val, s, ":", 2);
                                                        param.Add(s[0], s[1]);
                                                }
                                        }

                                        if (request["4"] == "input" || request["4"] == "internal")
                                        {
                                                ListeRoom::Instance().createInput(param, room);
                                        }
                                        else if (request["4"] == "output")
                                        {
                                                ListeRoom::Instance().createOutput(param, room);
                                        }
                                        else if (request["4"] == "camera")
                                        {
                                                if (!param.Exists("name")) param.Add("name", "Camera");
                                                if (!param.Exists("type")) param.Add("type", "planet");
                                                if (!param.Exists("host")) param.Add("host", "192.168.0.1");
                                                if (!param.Exists("port")) param.Add("port", "80");
                                                if (!param.Exists("iid")) param.Add("iid", Calaos::get_new_id("input_"));
                                                if (!param.Exists("oid")) param.Add("oid", Calaos::get_new_id("output_"));
                                                if (!param.Exists("id"))
                                                        param.Add("id", param["iid"] + "_" + param["oid"]);

                                                std::string type = param["type"];
                                                IPCam *cam = IOFactory::CreateIPCamera(type, param);
                                                if (cam)
                                                {
                                                        CamManager::Instance().Add(cam);
                                                        room->AddInput(cam->get_input());
                                                        room->AddOutput(cam->get_output());

                                                        string sig = "new_input id:";
                                                        sig += cam->get_input()->get_param("id") + " ";
                                                        sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                                        sig += url_encode(string("room_type:") + room->get_type());
                                                        IPC::Instance().SendEvent("events", sig);

                                                        sig = "new_output id:";
                                                        sig += cam->get_output()->get_param("id") + " ";
                                                        sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                                        sig += url_encode(string("room_type:") + room->get_type());
                                                        IPC::Instance().SendEvent("events", sig);
                                                }
                                        }
                                        else if (request["4"] == "audio")
                                        {
                                                if (!param.Exists("name")) param.Add("name", "Audio");
                                                if (!param.Exists("type")) param.Add("type", "slim");
                                                if (!param.Exists("host")) param.Add("host", "192.168.0.1");
                                                if (!param.Exists("port")) param.Add("port", "9090");
                                                if (!param.Exists("iid")) param.Add("iid", Calaos::get_new_id("input_"));
                                                if (!param.Exists("oid")) param.Add("oid", Calaos::get_new_id("output_"));
                                                if (!param.Exists("id")) param.Add("id", "please replace by the MAC address");

                                                std::string type = param["type"];
                                                AudioPlayer *player = IOFactory::CreateAudio(type, param);

                                                if (player)
                                                {
                                                        if (AudioManager::Instance().get_size() <= 0)
                                                                AudioManager::Instance().Add(player, param["host"]);
                                                        else
                                                                AudioManager::Instance().Add(player);
                                                        room->AddOutput(player->get_output());
                                                        room->AddInput(player->get_input());

                                                        string sig = "new_input id:";
                                                        sig += player->get_input()->get_param("id") + " ";
                                                        sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                                        sig += url_encode(string("room_type:") + room->get_type());
                                                        IPC::Instance().SendEvent("events", sig);

                                                        sig = "new_output id:";
                                                        sig += player->get_output()->get_param("id") + " ";
                                                        sig += url_encode(string("room_name:") + room->get_name()) + " ";
                                                        sig += url_encode(string("room_type:") + room->get_type());
                                                        IPC::Instance().SendEvent("events", sig);
                                                }
                                        }

                                        result.Add("4", "ok");
                                }
                        }

                }
        }

        //return the result
        ProcessDone_signal sig;
        sig.connect(callback);
        sig.emit(result);
}
