/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL & xAP protocol

  This file is part of xPPLib.

    xPPLib is free software : you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    xPPLib is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with xPPLib.  If not, see <http://www.gnu.org/licenses/>.
*/
/***************************************************************************************************/

#include <sstream>
#include "SchemaHbeat.h"

namespace xPL
{

using namespace std;

/****************************************************************************************************/
/*** Schema hbeat.end                                                                               */
SchemaHbeatEnd::SchemaHbeatEnd() : SchemaObject(ISchema::stat, "hbeat", "end")
{
}

SchemaHbeatEnd::~SchemaHbeatEnd()
{
}

void SchemaHbeatEnd::Check()
{
}

/****************************************************************************************************/
/*** Schema hbeat.basic                                                                             */
SchemaHbeatBasic::SchemaHbeatBasic() : SchemaObject(ISchema::stat, "hbeat", "basic")
{
}

SchemaHbeatBasic::SchemaHbeatBasic(int interval) : SchemaObject(ISchema::stat, "hbeat", "basic")
{
    SetInterval(interval);
}

SchemaHbeatBasic::~SchemaHbeatBasic()
{
}

int SchemaHbeatBasic::GetInterval()
{
    return GetValue<int>("interval");
}

void SchemaHbeatBasic::SetInterval(int interval)
{
    SetValue<int>("interval", interval);
}

void SchemaHbeatBasic::Check()
{
    if(GetInterval()==0) throw SchemaHbeatBasic::Exception(0x0101, "SchemaHbeat::Check() : Interval is mandatory in hbeat schema");
}

/****************************************************************************************************/
/*** Schema hbeat.app                                                                               */
SchemaHbeatApp::SchemaHbeatApp() : SchemaHbeatBasic()
{
    SetType("app");
}

SchemaHbeatApp::SchemaHbeatApp(int interval, int port, const string& remoteIP) : SchemaHbeatBasic()
{
    SetType("app");
    SetInterval(interval);
    SetPort(port);
    SetRemoteIP(remoteIP);
}

SchemaHbeatApp::~SchemaHbeatApp()
{
}

int SchemaHbeatApp::GetPort()
{
    return GetValue<int>("port");
}

void SchemaHbeatApp::SetPort(int port)
{
    SetValue<int>("port", port);
}

string SchemaHbeatApp::GetRemoteIP()
{
    return GetValue("remote-ip");
}

void SchemaHbeatApp::SetRemoteIP(const string& remoteIP)
{
    SetValue("remote-ip", remoteIP);
}

void SchemaHbeatApp::Check()
{
    SchemaHbeatBasic::Check();
    if(GetPort()==0)      throw SchemaHbeatApp::Exception(0x0102, "SchemaHbeat::Check() : Port is mandatory in hbeat schema");
    if(GetRemoteIP()=="") throw SchemaHbeatApp::Exception(0x0103, "SchemaHbeat::Check() : Remote-ip is mandatory in hbeat schema");
}

/****************************************************************************************************/
/*** Schema hbeat.request                                                                           */
SchemaHbeatRequest::SchemaHbeatRequest() : SchemaObject(ISchema::cmnd, "hbeat", "request")
{
    AddValue("command", "request");
}

SchemaHbeatRequest::~SchemaHbeatRequest()
{
}

void SchemaHbeatRequest::Check()
{
}

}
