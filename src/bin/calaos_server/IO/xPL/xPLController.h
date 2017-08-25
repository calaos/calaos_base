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
#ifndef xPLController_H
#define xPLController_H

#include "Calaos.h"
#include "Timer.h"
#include "xPLLib/xPLDevice.h"
#include "xPLLib/Schemas/SchemaControl.h"
#include "xPLLib/Schemas/SchemaSensor.h"

namespace uvw {
//Forward declare classes here to prevent long build time
//because of uvw.hpp being header only
class UDPHandle;
}

class xPLInfoSensor
{
private:

public:
    xPLInfoSensor() { }

    float AnalogVal;
    string StringVal;
};

class xPLSockWrapper : public xPL::xPLDevice::ISockSend
{
public:
    xPLSockWrapper();
    ~xPLSockWrapper();

    bool IsOpen();
    void Send(std::string const& xplmsg);
};

class xPLController
{

protected:
    xPLController();
    
    void udpListenData(const char *data, std::size_t length, string remoteIp, int remotePort);

    std::shared_ptr<uvw::UDPHandle> m_UdpRecvHandle;
    xPLSockWrapper m_xPLSockWrapper;

    std::unordered_map<string, sigc::signal<void, xPLInfoSensor*>> m_sensorsChangeCb;
    Timer *m_timer;
    xPL::xPLDevice m_xPLDevice;


public:
    ~xPLController();

    //Singleton
    static xPLController &Instance();
    
    std::string localAddress();
    void RegisterSensor(const string& source, const string& device, sigc::slot<void, xPLInfoSensor*> callback);
    void SetValue(const string& source, const string& device, const string& value);

};

#endif
