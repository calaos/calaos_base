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
#include "xPLController.h"
#include "uv.h"
#include "uvw/src/uvw.hpp"


xPLSockWrapper::xPLSockWrapper()
{
}

xPLSockWrapper::~xPLSockWrapper()
{
}

bool xPLSockWrapper::IsOpen()
{
    return true;
}

void xPLSockWrapper::Send(string const& xplMsg)
{
    std::shared_ptr<uvw::UDPHandle> udpSenderHandle;
    int dataSize = xplMsg.length();
    auto dataWrite = std::unique_ptr<char[]>(new char[dataSize]);
    std::copy(xplMsg.begin(), xplMsg.end(), dataWrite.get());

    udpSenderHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();
    udpSenderHandle->bind("255.255.255.255", 3865);
    udpSenderHandle->broadcast(true);
    udpSenderHandle->send("255.255.255.255", 3865, std::move(dataWrite), dataSize);
    udpSenderHandle->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &)
    {
        cErrorDom("network") << "xPL UDP client error : " << ev.what();
    });
    udpSenderHandle->once<uvw::SendEvent>([](auto &, auto &h)
    {
        h.close();
    });
}

xPLController::xPLController()
{
    int portTCP;

    //Create listening udp server on local port to receive frame from xPL network
    m_UdpRecvHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();

    m_UdpRecvHandle->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        this->udpListenData(ev.data.get(), ev.length, ev.sender.ip, ev.sender.port);
    });

    m_UdpRecvHandle->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &)
    {
        cErrorDom("network") << "xPL UDP server error : " << ev.what();
    });

    portTCP = discoverxPLPort();
    m_UdpRecvHandle->broadcast(true);
    cInfoDom("xPL") << "Listening on port " << portTCP;
    m_UdpRecvHandle->bind("0.0.0.0", portTCP, uvw::UDPHandle::Bind::REUSEADDR);
    m_UdpRecvHandle->recv();

    //xPL Device initialisation
    m_xPLDevice.Initialisation("fragxpl", "calaos", "default");
    m_xPLDevice.SetAppName("xPL Calaos", getCalaosVersion());
    m_xPLDevice.SetRecvSockInfo(localAddress(), portTCP);
    m_xPLDevice.SetSendSockCallback(&m_xPLSockWrapper);     
    m_xPLDevice.Open();

    //Heartbeat each 5 minutes
    m_timer = new Timer((double)5*60, [=]()
    {
        m_xPLDevice.SendHeartBeat(true);
    });
}

xPLController::~xPLController()
{
    m_UdpRecvHandle->stop();
    m_UdpRecvHandle->close();
    DELETE_NULL(m_timer);
    m_xPLDevice.Close();
}

string xPLController::getCalaosVersion()
{
    string packageString(PACKAGE_STRING);

    if(packageString.substr(0, 8)!="calaos v")
        return "unknown";
    
    return packageString.substr(8);
}

int xPLController::discoverxPLPort()
{
    int portHub = 3865;
    int portMin = 49152-1;
    int portMax = 65535;
    int portTCP;
    uv_udp_t sock;
    struct sockaddr_in addr;
    int r;


    r = uv_udp_init(uv_default_loop(), &sock);
    if(r!=0) return 3865;
    
    portTCP = portHub;
    do
    {
        uv_ip4_addr("0.0.0.0", portTCP, &addr);
        r = uv_udp_bind(&sock, (const struct sockaddr*) &addr, UV_UDP_REUSEADDR);
        if(r==0) break;
        if(portTCP==portHub) portTCP=portMin;
        portTCP++;
    } while(portTCP!=portMax);
    uv_udp_recv_stop(&sock);

    if(r!=0) return 3865;
    return portTCP;  
}

string xPLController::localAddress()
{
    std::vector<uvw::InterfaceAddress> interfaceAdrs;
    std::vector<uvw::InterfaceAddress>::iterator it;
  
    interfaceAdrs = uvw::Utilities::interfaceAddresses();
    it = interfaceAdrs.begin();
    while(it != interfaceAdrs.end())
    {
        if((!it->internal)&&(it->address.ip.find('.')!=string::npos))
            return it->address.ip;   
        ++it;
    }    
    return "127.0.0.1";
}

xPLController &xPLController::Instance()
{
    static xPLController myInstance;

    return myInstance;
}

void xPLController::registerSensor(const string& source, const string& sensor, sigc::slot<void, xPLInfoSensor*> callback)
{
    string key = source;
    key += ":";
    key += sensor;
    m_sensorsChangeCb[key].connect(callback);
    getValue(source, sensor);
}

void xPLController::getValue(const string& source, const string& sensor)
{
    xPL::SchemaSensorRequest ssr;
    ssr.SetDeviceName(sensor);
    m_xPLDevice.SendxPLMessage(&ssr, source);
}

void xPLController::setValue(const string& source, const string& device, const string& value)
{
    xPL::SchemaControlBasic scb;
    scb.SetDeviceName(device);
    scb.SetCurrent(value);    
    m_xPLDevice.SendxPLMessage(&scb, source);
}

void xPLController::udpListenData(const char *data, std::size_t length, string remoteIp, int remotePort)
{
    string xPLRaw(data, length);
    xPL::SchemaObject xPLparse;
    string key;
    xPLInfoSensor infoSensor;
    std::unordered_map<string, sigc::signal<void, xPLInfoSensor*>>::iterator it;


    while(xPLRaw!="")
    {
        try
        {
            xPLRaw = xPLparse.Parse(xPLRaw);
        }
        catch(const xPL::SchemaObject::Exception &e)
        {
            cErrorDom("xPLcontroller") <<  "Failed to parse : " << e.what();
            return;
        }
        
        if(m_xPLDevice.MsgForMe(xPLparse) && m_xPLDevice.MsgAnswer(xPLparse)) continue;
        
        xPL::ISchema::MsgType msgType = xPLparse.GetMsgType();
        if((msgType!=xPL::ISchema::MsgType::stat)&&(msgType!=xPL::ISchema::MsgType::trig)) continue;
        if(xPLparse.GetClass()!="sensor") continue;
        if(xPLparse.GetType()!="basic") continue;
        
        key = xPLparse.GetSource()+":"+xPLparse.GetValue("device");

        it = m_sensorsChangeCb.find(key);
        if(it != m_sensorsChangeCb.end())
        {
            infoSensor.StringVal = xPLparse.GetValue("current");
            infoSensor.AnalogVal = xPLparse.GetValue<float>("current"); 
            (it->second).emit(&infoSensor);
        }
    }
}
