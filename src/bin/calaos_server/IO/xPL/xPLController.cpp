/******************************************************************************
**  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include <xPLController.h>
#include "uvw/src/uvw.hpp"

xPLController::xPLController()
{
    //xPL Device initialisation
    //m_xPLDevice.Initialisation("fragxpl", "calaos", "default");
    //m_xPLDevice.SetAppName("xPL Calaos", "1.0.0.0");    //CALAOS_VERSION ?
    
    //m_xPLDevice.SendHeartBeat();    //A lancer régulièrement
    /*timer = new Timer((double)t / 1000., [=]()
      {
          KNXCtrl::Instance(get_param("host"))->readValue(knxBase->getReadGroupAddr());
      });
    */

    //Create listening udp server on local port to receive frame from xPL network
    udpSrvHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();

    udpSrvHandle->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        this->udpListenData(ev.data.get(), ev.length, ev.sender.ip, ev.sender.port);
    });

    udpSrvHandle->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &)
    {
        cErrorDom("network") << "UDP server error: " << ev.what();
    });

    udpSrvHandle->bind("0.0.0.0", 3865, uvw::UDPHandle::Bind::REUSEADDR);
    udpSrvHandle->recv();
}

xPLController::~xPLController()
{
    udpSrvHandle->stop();
    udpSrvHandle->close();
    //m_xPLDevice.Close();
    DELETE_NULL(m_timer);
}

xPLController &xPLController::Instance()
{
    static xPLController myInstance;

    return myInstance;
}

void xPLController::RegisterSensor(const string& source, const string& sensor, sigc::slot<void, xPLInfoSensor*> callback)
{
    string key = source;
    key += ":";
    key += sensor;
    
    m_sensorsChangeCb[key].connect(callback);
    //TODO Envoyer un message xPL pour demander la valeur 
}

void xPLController::SetValue(const string& source, const string& device, const string& value)
{
    auto loop = uvw::Loop::getDefault();
    auto udp_con = loop->resource<uvw::UDPHandle>();
    string xplMsg;

    xplMsg = "TEST";  //Implémenter la construction du message xPL
    udp_con->send("0.0.0.0", 3865, (char *)xplMsg.c_str(), xplMsg.size());
    udp_con->once<uvw::SendEvent>([](auto &, auto &h)
    {
        h.close();
    });
}

void xPLController::udpListenData(const char *data, std::size_t length, string remoteIp, int remotePort)
{
/*
    string xPLRaw(data, length);
    xPL::SchemaObject xPLparse;
    string key;
    std::unordered_map<string, sigc::signal<void, xPLInfoSensor*>>::iterator it;
    
    while(xPLRaw!="")
    {
        try
        {
            xPLRaw = xPLparse.Parse(xPLRaw);
        }
        catch(const SchemaObject::Exception &e)
        {
            cErrorDom("xPLcontroller") <<  "Failed to parse : " << e.what();
            return;
        }
        
        if(m_xPLDevice.MsgForMe(xPLparse) && (m_xPLDevice.MsgAnswer(xPLparse)) continue;
        
        key = xPLparse.GetSource()+":"+xPLparse.GetValue("device");
        it = m_sensorsChangeCb.find(key);
        if(it != m_sensorsChangeCb.end()
        {
            it->emit();
        }
    }
*/
}
