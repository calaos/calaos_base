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

#include "SchemaSensor.h"

namespace xPL
{

using namespace std;

/****************************************************************************************************/
/*** Schema sensor.basic                                                                            */
SchemaSensorBasic::SchemaSensorBasic() : SchemaObject(ISchema::trig, "sensor", "basic")
{
}

SchemaSensorBasic::SchemaSensorBasic(const string& name, SchemaSensorTypeUtility::SensorType type) : SchemaObject(ISchema::trig, "sensor", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
}

SchemaSensorBasic::SchemaSensorBasic(const string& name, SchemaSensorTypeUtility::SensorType type, float value) : SchemaObject(ISchema::trig, "sensor", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
    SetCurrent(value);
}

SchemaSensorBasic::SchemaSensorBasic(const string& name, SchemaSensorTypeUtility::SensorType type, const string& value) : SchemaObject(ISchema::trig, "sensor", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
    SetCurrent(value);
}

SchemaSensorBasic::SchemaSensorBasic(const string& name, SchemaSensorTypeUtility::SensorType type, float value, const string& unit) : SchemaObject(ISchema::trig, "sensor", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
    SetCurrent(value);
    SetUnit(unit);
}

SchemaSensorBasic::SchemaSensorBasic(const string& name, SchemaSensorTypeUtility::SensorType type, const string& value, const string& unit) : SchemaObject(ISchema::trig, "sensor", "basic")
{
    SetDeviceName(name);
    SetDeviceType(type);
    SetCurrent(value);
    SetUnit(unit);
}

SchemaSensorBasic::~SchemaSensorBasic()
{
}

void SchemaSensorBasic::SetDevice(const string& name, SchemaSensorTypeUtility::SensorType type)
{
    SetDeviceName(name);
    SetDeviceType(type);
}
void SchemaSensorBasic::SetDeviceName(const string& name)
{
    SetValue("device", name);
}
void SchemaSensorBasic::SetDeviceType(SchemaSensorTypeUtility::SensorType type)
{
    SetValue("type", SchemaSensorTypeUtility::ToString(type));
}

void SchemaSensorBasic::SetCurrent(float value)
{
    SetValue<float>("current", value);
}
void SchemaSensorBasic::SetCurrent(const std::string& value)
{
    SetValue("current", value);
}

void SchemaSensorBasic::SetUnit(const string& unit)
{
    if(unit=="") return;
    SetValue("unit", unit);
}

float SchemaSensorBasic::GetCurrent()
{
    return GetValue<float>("current");
}

void SchemaSensorBasic::Check()
{
}

/****************************************************************************************************/
/*** Schema sensor.request                                                                          */
SchemaSensorRequest::SchemaSensorRequest() : SchemaObject(ISchema::cmnd, "sensor", "request")
{
    AddValue("request", "current");
}

SchemaSensorRequest::~SchemaSensorRequest()
{
}

void SchemaSensorRequest::SetDevice(const string& name, SchemaSensorTypeUtility::SensorType type)
{
    SetDeviceName(name);
    SetDeviceType(type);
}
void SchemaSensorRequest::SetDeviceName(const string& name)
{
    SetValue("device", name);
}
void SchemaSensorRequest::SetDeviceType(SchemaSensorTypeUtility::SensorType type)
{
    SetValue("type", SchemaSensorTypeUtility::ToString(type));
}

}

