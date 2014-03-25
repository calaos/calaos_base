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
#include <Room.h>
#include <ListeRule.h>
#include <ListeRoom.h>
#include <IPC.h>
#include <AVReceiver.h>

using namespace Calaos;

Room::Room(string _name, string _type, int _hits):
    name(_name),
    type(_type),
    hits(_hits)
{
    cDebugDom("room") << "Room::Room(" << name << ", " << type << "): Ok";
}

Room::~Room()
{
    while (inputs.size() > 0)
        ListeRoom::Instance().deleteIO(inputs[0]);

    while (outputs.size() > 0)
        ListeRoom::Instance().deleteIO(outputs[0]);

    inputs.clear();
    outputs.clear();

    cDebugDom("room");
}

void Room::AddInput(Input *in)
{
    inputs.push_back(in);

    cDebugDom("room") << "(" << in->get_param("id") << "): Ok";
}

void Room::AddOutput(Output *out)
{
    outputs.push_back(out);

    cDebugDom("room") << "(" << out->get_param("id") << "): Ok";
}

void Room::RemoveInput(int pos, bool del)
{
    string sig = "delete_input ";
    sig += inputs[pos]->get_param("id") + " ";
    sig += url_encode(string("room_name:") + name) + " ";
    sig += url_encode(string("room_type:") + type);
    IPC::Instance().SendEvent("events", sig);

    vector<Input *>::iterator iter = inputs.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    if (del) delete inputs[pos];
    inputs.erase(iter);

    cDebugDom("room");
}

void Room::RemoveOutput(int pos, bool del)
{
    string sig = "delete_output ";
    sig += outputs[pos]->get_param("id") + " ";
    sig += url_encode(string("room_name:") + name) + " ";
    sig += url_encode(string("room_type:") + type);
    IPC::Instance().SendEvent("events", sig);

    vector<Output *>::iterator iter = outputs.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    if (del) delete outputs[pos];
    outputs.erase(iter);

    cDebugDom("room");
}

void Room::RemoveInputFromRoom(Input *in)
{
    vector<Input *>::iterator it = find(inputs.begin(), inputs.end(), in);
    if (it != inputs.end())
    {
        inputs.erase(it);

        string sig = "modify_room ";
        sig += url_encode(string("input_del:") + in->get_param("id")) + " ";
        sig += url_encode(string("room_name:") + get_name()) + " ";
        sig += url_encode(string("room_type:") + get_type());
        IPC::Instance().SendEvent("events", sig);
    }
}

void Room::RemoveOutputFromRoom(Output *out)
{
    vector<Output *>::iterator it = find(outputs.begin(), outputs.end(), out);
    if (it != outputs.end())
    {
        outputs.erase(it);

        string sig = "modify_room ";
        sig += url_encode(string("output_del:") + out->get_param("id")) + " ";
        sig += url_encode(string("room_name:") + get_name()) + " ";
        sig += url_encode(string("room_type:") + get_type());
        IPC::Instance().SendEvent("events", sig);
    }
}

void Room::set_name(std::string &s)
{
    string sig = "modify_room ";
    sig += url_encode(string("old_room_name:") + name) + " ";
    sig += url_encode(string("new_room_name:") + s) + " ";
    sig += url_encode(string("room_type:") + type);
    IPC::Instance().SendEvent("events", sig);

    name = s;
}

void Room::set_type(std::string &s)
{
    string sig = "modify_room ";
    sig += url_encode(string("old_room_type:") + type) + " ";
    sig += url_encode(string("new_room_type:") + s) + " ";
    sig += url_encode(string("room_name:") + name);
    IPC::Instance().SendEvent("events", sig);

    type = s;
}

void Room::set_hits(int h)
{
    string sig = "modify_room ";
    sig += url_encode(string("old_room_hits:") + Utils::to_string(hits)) + " ";
    sig += url_encode(string("new_room_hits:") + Utils::to_string(h)) + " ";
    sig += url_encode(string("room_name:") + name) + " ";
    sig += url_encode(string("room_type:") + type);
    IPC::Instance().SendEvent("events", sig);

    hits = h;
}

bool Room::LoadFromXml(TiXmlElement *room_node)
{
    TiXmlElement *node = room_node->FirstChildElement();
    for(; node; node = node->NextSiblingElement())
    {
        if (node->ValueStr() == "calaos:input")
        {
            Input *in = IOFactory::CreateInput(node);
            if (in)
            {
                AddInput(in);

                InputTimer *o = dynamic_cast<InputTimer *>(in);
                if (o) AddOutput(o);

                Scenario *sc = dynamic_cast<Scenario *>(in);
                if (sc) AddOutput(sc);
            }
        }
        else if (node->ValueStr() == "calaos:output")
        {
            Output *out = IOFactory::CreateOutput(node);
            if (out) AddOutput(out);
        }
        else if (node->ValueStr() == "calaos:audio")
        {
            AudioPlayer *player = IOFactory::CreateAudio(node);
            if (player)
            {
                if (AudioManager::Instance().get_size() <= 0)
                    AudioManager::Instance().Add(player, player->get_param("host"));
                else
                    AudioManager::Instance().Add(player);

                AddInput(player->get_input());
                AddOutput(player->get_output());
            }
        }
        else if (node->ValueStr() == "calaos:internal")
        {
            Input *in = IOFactory::CreateInput(node);
            if (in)
            {
                Internal *intern = dynamic_cast<Internal *>(in);
                if (intern)
                {
                    AddInput(intern);
                    AddOutput(intern);
                }
            }
        }
        else if (node->ValueStr() == "calaos:camera")
        {
            IPCam *camera = IOFactory::CreateIPCamera(node);
            if (camera)
            {
                CamManager::Instance().Add(camera);

                AddInput(camera->get_input());
                AddOutput(camera->get_output());
            }
        }
        else if (node->ValueStr() == "calaos:avr")
        {
            Output *o = IOFactory::CreateOutput(node);
            if (o)
            {
                IOAVReceiver *receiver = dynamic_cast<IOAVReceiver *>(o);
                if (receiver)
                {
                    AddInput(receiver);
                    AddOutput(receiver);
                }
            }
        }
    }

    return true;
}

bool Room::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *room_node = new TiXmlElement("calaos:room");
    room_node->SetAttribute("name", name);
    room_node->SetAttribute("type", type);
    room_node->SetAttribute("hits", Utils::to_string(hits));
    node->LinkEndChild(room_node);

    for (int i = 0;i < get_size_in();i++)
    {
        Input *input = get_input(i);

        if (input->get_param("type") == "WIDigital" || input->get_param("type") == "WIDigitalBP" ||
            input->get_param("type") == "WIDigitalTriple" || input->get_param("type") == "WITemp" ||
            input->get_param("type") == "OWTemp" || input->get_param("type") == "WIDigitalLong" ||
            input->get_param("type") == "scenario" || input->get_param("type") == "WIDigitalBP" ||
            input->get_param("type") == "InputTime" || input->get_param("type") == "InputTimer" ||
            input->get_param("type") == "InternalBool" ||
            input->get_param("type") == "InternalInt" ||
            input->get_param("type") == "InternalString" ||
            input->get_param("type") == "InPlageHoraire" ||
            input->get_param("type") == "WIAnalog" ||
            input->get_param("type") == "WebAnalogIn" ||
            input->get_param("type") == "GpioInputSwitch")
        {
            input->SaveToXml(room_node);
        }
    }

    for (int i = 0;i < get_size_out();i++)
    {
        Output *output = get_output(i);

        if (output->get_param("type") == "WODigital" || output->get_param("type") == "OutputFake" ||
            output->get_param("type") == "WONeon" || output->get_param("type") == "WOVolet" ||
            output->get_param("type") == "X10Output" || output->get_param("type") == "WODali" ||
            output->get_param("type") == "WODaliRVB" || output->get_param("type") == "WOVoletSmart" ||
            output->get_param("type") == "WOAnalog" || output->get_param("type") == "AVReceiver" ||
            output->get_param("type") == "GpioOutputSwitch")
        {
            output->SaveToXml(room_node);
        }

        AudioOutput *audio_output = dynamic_cast<AudioOutput *>(output);
        if (audio_output)
        {
            audio_output->get_player()->SaveToXml(room_node);
        }

        CamOutput *camera_output = dynamic_cast<CamOutput *>(output);
        if (camera_output)
        {
            camera_output->get_cam()->SaveToXml(room_node);
        }
    }

    return true;
}
