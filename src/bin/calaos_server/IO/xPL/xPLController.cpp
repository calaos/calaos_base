/******************************************************************************
**  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include "libuvw.h"


xPLSockWrapper::xPLSockWrapper()
{
    Connect();
}

xPLSockWrapper::~xPLSockWrapper()
{
    if(m_UdpSenderHandle)
        m_UdpSenderHandle->close();
}

void xPLSockWrapper::Connect()
{
    m_UdpSenderHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();
    m_UdpSenderHandle->bind("255.255.255.255", XPL_DEFAULT_PORT, uvw::UDPHandle::Bind::REUSEADDR);
    m_UdpSenderHandle->broadcast(true);

    m_UdpSenderHandle->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &h)
    {
        cErrorDom("xpl") << "xPL UDP client error : " << ev.what();
        h.once<uvw::CloseEvent>([this](auto &, auto &)
        {
            Timer::singleShot(2.5, (sigc::slot<void>)sigc::mem_fun(*this, &xPLSockWrapper::Connect));
        });
        h.close();
    });
}

bool xPLSockWrapper::IsOpen()
{
    return true;
}

void xPLSockWrapper::Send(string const& xplMsg)
{
    int dataSize = xplMsg.length();
    auto dataWrite = std::unique_ptr<char[]>(new char[dataSize]);
    std::copy(xplMsg.begin(), xplMsg.end(), dataWrite.get());

    m_UdpSenderHandle->send("255.255.255.255", XPL_DEFAULT_PORT, std::move(dataWrite), dataSize);
}

xPLController::xPLController()
{
    //xPL Device initialisation
    m_xPLDevice.Initialisation("fragxpl", "calaos", "default");
    m_xPLDevice.SetAppName("xPL Calaos", PACKAGE_VERSION);
    m_xPLDevice.SetSendSockCallback(&m_xPLSockWrapper);

    Connect();

    //Heartbeat each 5 minutes
    m_timer = new Timer((double)XPL_HEARTBEAT_PERIOD, [=]()
    {
        m_xPLDevice.SendHeartBeat(true);
    });
}

xPLController::~xPLController()
{
    if(m_UdpRecvHandle!=nullptr)
    {
        m_UdpRecvHandle->stop();
        m_UdpRecvHandle->close();
    }
    DELETE_NULL(m_timer);
    m_xPLDevice.Close();
}

void xPLController::Connect()
{
    int portTCP;


    m_UdpRecvHandle = uvw::Loop::getDefault()->resource<uvw::UDPHandle>();

    m_UdpRecvHandle->on<uvw::UDPDataEvent>([this](const uvw::UDPDataEvent &ev, auto &)
    {
        this->udpListenData(ev.data.get(), ev.length, ev.sender.ip, ev.sender.port);
    });

    m_UdpRecvHandle->once<uvw::ErrorEvent>([this](const uvw::ErrorEvent &ev, uvw::UDPHandle &h)
    {
        cErrorDom("xpl") << "xPL UDP server error : " << ev.what();
        h.once<uvw::CloseEvent>([this](auto &, auto &)
        {
            Timer::singleShot(2.5, (sigc::slot<void>)sigc::mem_fun(*this, &xPLController::Connect));
        });
        h.close();
    });

    auto listenAddr = Utils::get_config_option("listen_address");
    if (listenAddr == "")
        listenAddr = "0.0.0.0";

    portTCP = discoverxPLPort();
    m_UdpRecvHandle->broadcast(true);
    cInfoDom("xpl") << "Listening on port " << portTCP;
    m_UdpRecvHandle->bind(listenAddr, portTCP, uvw::UDPHandle::Bind::REUSEADDR);
    m_UdpRecvHandle->recv();
    m_xPLDevice.SetRecvSockInfo(localAddress(), portTCP);
    m_xPLDevice.Close();
    m_xPLDevice.Open();
}

int xPLController::discoverxPLPort()
{
    int portTCP;
    uv_udp_t sock;
    struct sockaddr_in addr;
    int r;


    r = uv_udp_init(uv_default_loop(), &sock);
    if(r!=0) return XPL_DEFAULT_PORT;

    portTCP = XPL_DEFAULT_PORT;
    do
    {
        uv_ip4_addr("0.0.0.0", portTCP, &addr);
        r = uv_udp_bind(&sock, (const struct sockaddr*) &addr, UV_UDP_REUSEADDR);
        if(r==0) break;
        if(portTCP == XPL_DEFAULT_PORT) portTCP = XPL_PORT_LOWER_BOUND-1;
        portTCP++;
    } while(portTCP != XPL_PORT_UPPER_BOUND);
    uv_udp_recv_stop(&sock);

    if(r!=0) return XPL_DEFAULT_PORT;
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
            cErrorDom("xpl") <<  "Failed to parse : " << e.what();
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
