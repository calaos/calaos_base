/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "AVReceiver.h"
#include "AVRManager.h"
#include "IOFactory.h"
#include "EventManager.h"
#include "libuvw.h"

using namespace Calaos;

REGISTER_IO_USERTYPE(AVReceiver, IOAVReceiver)

#define AVR_TIMEOUT      40.0
#define AVR_RECONNECT    10.0

AVReceiver::AVReceiver(Params &p, int default_port, int _connection_type):
    ref_count(0),
    params(p),
    isConnected(false),
    volume_main(0),
    volume_zone2(0),
    volume_zone3(0),
    power_main(false),
    power_zone2(false),
    power_zone3(false),
    source_main(0),
    source_zone2(0),
    source_zone3(0),
    connection_type(_connection_type)
{
    cDebugDom("output") << params["id"];

    if (!params.Exists("visible")) params.Add("visible", "false");

    host = params["host"];
    port = default_port;

    if (params.Exists("port"))
        from_string(params["port"], port);

    timerConnReconnect();
}

AVReceiver::~AVReceiver()
{
    if (conHandle && conHandle->active())
    {
        conHandle->stop();
        conHandle->close();
    }
}

void AVReceiver::timerConnReconnect()
{
    cDebugDom("output") << "Connecting to " << host << ":" << port;

    conHandle = uvw::Loop::getDefault()->resource<uvw::TcpHandle>();
    conHandle->connect(host, port);

    conHandle->once<uvw::ConnectEvent>([this](auto &, uvw::TcpHandle &h)
    {
        cDebugDom("output") << "connection established";
        h.read();

        isConnected = true;
        this->connectionEstablished();
    });

    conHandle->once<uvw::ErrorEvent>([this](auto &ev, uvw::TcpHandle &h)
    {
        cErrorDom("squeezebox") << "Notif connection error: " << ev.what();
        h.close();
        h.once<uvw::CloseEvent>([this](auto &, auto &)
        {
            Timer::singleShot(AVR_RECONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &AVReceiver::timerConnReconnect));
        });
    });

    conHandle->once<uvw::EndEvent>([this](auto &, uvw::TcpHandle &h)
    {
        cWarningDom("output") << "Main Connection closed !";
        cWarningDom("output") << "Trying to reconnect...";
        h.close();
        isConnected = false;
        h.once<uvw::CloseEvent>([this](auto &, auto &)
        {
            Timer::singleShot(AVR_RECONNECT, (sigc::slot<void>)sigc::mem_fun(*this, &AVReceiver::timerConnReconnect));
        });
    });

    conHandle->on<uvw::DataEvent>([this](const uvw::DataEvent &ev, auto &)
    {
        if (connection_type == AVR_CON_CHAR)
        {
            string msg((char *)ev.data.get(), ev.length);
            this->dataGet(msg);
        }
        else if (connection_type == AVR_CON_BYTES)
        {
            char *cdata = (char *)ev.data.get();
            vector<char> d(cdata, cdata + ev.length);

            //We don't know how to handle these messages,
            //so we delegate the processing to the child class
            //processMessage(vector<char>) has to be inherited !
            this->processMessage(d);
        }
    });
}

void AVReceiver::dataGet(string msg)
{
    if (msg.find('\n') == string::npos &&
        msg.find('\r') == string::npos)
    {
        //We have not a complete paquet yet, buffurize it.
        recv_buffer += msg;

        cDebugDom("output") << "Bufferize data.";

        return;
    }

    if (!recv_buffer.empty())
    {
        //Put last data in buffer
        recv_buffer += msg;
        msg = recv_buffer;
        recv_buffer.clear();
    }

    replace_str(msg, "\r\n", "\n");
    replace_str(msg, "\r", "\n");

    vector<string> tokens;
    split(msg, tokens, "\n");

    cDebugDom("output") << "Got " << tokens.size() << " messages.";

    for(uint i = 0; i < tokens.size(); i++)
        processMessage(tokens[i]);
}

void AVReceiver::processMessage(string msg)
{
    cWarningDom("output") << "Should be inherited !";
}

void AVReceiver::processMessage(vector<char> msg)
{
    cWarningDom("output") << "Should be inherited !";
}

void AVReceiver::sendRequest(string request)
{
    if (!isConnected) return;

    cDebugDom("output") << "Command: " << request;

    request += command_suffix;

    int dataSize = request.length();
    auto dataWrite = std::unique_ptr<char[]>(new char[dataSize]);
    std::copy(request.begin(), request.end(), dataWrite.get());
    conHandle->write(std::move(dataWrite), dataSize);
}

void AVReceiver::sendRequest(vector<char> request)
{
    if (!isConnected) return;

    cDebugDom("output") << request.size() << " bytes";

    conHandle->write((char *)&request[0], request.size());
}

bool AVReceiver::getPower(int zone)
{
    if (zone == 2) return power_zone2;
    if (zone == 3) return power_zone3;
    return power_main;
}

int AVReceiver::getVolume(int zone)
{
    if (zone == 2) return volume_zone2;
    if (zone == 3) return volume_zone3;
    return volume_main;
}

int AVReceiver::getInputSource(int zone)
{
    if (zone == 2) return source_zone2;
    if (zone == 3) return source_zone3;
    return source_main;
}

IOAVReceiver::IOAVReceiver(Params &p):
    IOBase(p, IOBase::IO_INOUT),
    zone(1)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("AVReceiver");
    ioDoc->descriptionSet(_("AVReceiver object to control network amplifier"));
    ioDoc->paramAdd("host", _("IP address of the device"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Port to use for connection"), 0, 65535, false);
    ioDoc->paramAddInt("zone", _("Zone of the amplifier (if supported)"), 0, 10, false);
    ioDoc->paramAdd("model", _("AVReceiver model. Supported: pioneer, denon, onkyo, marantz, yamaha"), IODoc::TYPE_STRING, true);

    ioDoc->actionAdd("power on", _("Switch receiver on"));
    ioDoc->actionAdd("power off", _("Switch receiver off"));
    ioDoc->actionAdd("volume 50", _("Set current volume"));
    ioDoc->actionAdd("source X", _("Change current input source"));
    ioDoc->actionAdd("custom XXXXXX", _("Send a custom command to receiver (if you know the protocol)"));

    get_params().Add("gui_type", "avreceiver");
    get_params().Add("visible", "false");
    if (get_params().Exists("zone"))
        from_string(get_param("zone"), zone);
    receiver = AVRManager::Instance().Create(p);

    if (zone == 1 && receiver)
        receiver->state_changed_1.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
    if (zone == 2 && receiver)
        receiver->state_changed_2.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
    if (zone == 3 && receiver)
        receiver->state_changed_3.connect(sigc::mem_fun(*this, &IOAVReceiver::statusChanged));
}

IOAVReceiver::~IOAVReceiver()
{
    AVRManager::Instance().Delete(receiver);
}

void IOAVReceiver::statusChanged(string _param, string value)
{
    EmitSignalIO();

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { _param, value } });
}

string IOAVReceiver::get_value_string()
{
    if (!receiver) return "";

    return Utils::to_string(receiver->getInputSource(zone));
}

map<string, string> IOAVReceiver::get_all_values_string()
{
    map<string, string> m;

    if (!receiver)
        return m;

    if (receiver->getPower(zone))
        m["power"] = "true";
    else
        m["power"] = "false";

    m["volume"] = Utils::to_string(receiver->getVolume(zone));
    m["input_source"] = Utils::to_string(receiver->getInputSource(zone));

    if (receiver->hasDisplay())
    {
        m["has_display"] = "true";
        m["display_text"] = receiver->getDisplayText();
    }
    else
        m["has_display"] = "false";

    return m;
}

bool IOAVReceiver::set_value(string val)
{
    if (!isEnabled() || !receiver) return false;

    cInfoDom("output") << get_param("id") << " got action, " << val;

    if (val == "power off" || val == "power false")
        receiver->Power(false, zone);
    else if (val == "power on" || val == "power true")
        receiver->Power(true, zone);
    else if (val.substr(0, 7) == "volume ")
    {
        val.erase(0, 7);
        int vol;
        from_string(val, vol);
        receiver->setVolume(vol, zone);
    }
    else if (val.substr(0, 7) == "source ")
    {
        val.erase(0, 7);
        int source;
        from_string(val, source);
        receiver->selectInputSource(source, zone);
    }
    else if (val.substr(0, 7) == "custom ")
    {
        val.erase(0, 7);
        receiver->sendCustomCommand(val);
    }

    return true;
}

bool IOAVReceiver::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:avr");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

map<string, string> IOAVReceiver::query_param(string key)
{
    map<string, string> m;

    if (!receiver) return m;

    if (key == "input_sources")
    {
        AVRList sources = receiver->getSources();
        AVRList::iterator it = sources.begin();
        for (;it != sources.end();it++)
            m[Utils::to_string((*it).first)] = (*it).second;
    }

    return m;
}
