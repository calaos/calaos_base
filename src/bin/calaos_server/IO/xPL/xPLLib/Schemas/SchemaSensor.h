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

#ifndef SCHEMASENSORBASIC_H
#define SCHEMASENSORBASIC_H

#include <string>
#include <string.h>
#include "SchemaObject.h"

namespace xPL
{

class SchemaSensorTypeUtility
{
    public:
        enum SensorType
        {
            unset = 0,
            battery,
            count,
            current,
            direction,
            distance,
            energy,
            fan,
            generic,
            humidity,
            illuminance,
            input,
            output,
            power,
            pressure,
            setpoint,
            speed,
            temp,
            uv,
            voltage,
            volume,
            weight
        };

        static std::string ToString(SensorType st)
        {
            switch(st)
            {
                case unset : return "unset";
                case battery : return "battery";
                case count : return "count";
                case current : return "current";
                case direction : return "direction";
                case distance : return "distance";
                case energy : return "energy";
                case fan : return "fan";
                case generic : return "generic";
                case humidity : return "humidity";
                case illuminance : return "illuminance";
                case input : return "input";
                case output : return "output";
                case power : return "power";
                case pressure : return "pressure";
                case setpoint : return "setpoint";
                case speed : return "speed";
                case temp : return "temp";
                case uv : return "uv";
                case voltage : return "voltage";
                case volume : return "volume";
                case weight : return "weight";
                default : return "unknown";
            }
        }

        static SensorType toSensorType(const std::string& typeStr)
        {
            if(typeStr=="battery") return SensorType::battery;
            if(typeStr=="count") return SensorType::count;
            if(typeStr=="current") return SensorType::current;
            if(typeStr=="direction") return SensorType::direction;
            if(typeStr=="distance") return SensorType::distance;
            if(typeStr=="energy") return SensorType::energy;
            if(typeStr=="fan") return SensorType::fan;
            if(typeStr=="generic") return SensorType::generic;
            if(typeStr=="humidity") return SensorType::humidity;
            if(typeStr=="illuminance") return SensorType::illuminance;
            if(typeStr=="input") return SensorType::input;
            if(typeStr=="output") return SensorType::output;
            if(typeStr=="power") return SensorType::power;
            if(typeStr=="pressure") return SensorType::pressure;
            if(typeStr=="setpoint") return SensorType::setpoint;
            if(typeStr=="speed") return SensorType::speed;
            if(typeStr=="temp") return SensorType::temp;
            if(typeStr=="uv") return SensorType::uv;
            if(typeStr=="voltage") return SensorType::voltage;
            if(typeStr=="volume") return SensorType::volume;
            if(typeStr=="weight") return SensorType::weight;
            return SensorType::unset;
        }

};

/// \brief    Class to manage schema sensor.basic xPL message
class SchemaSensorBasic : public SchemaObject
{
    public:
        /// \brief    Constructor of SensorBasic
        SchemaSensorBasic();
        SchemaSensorBasic(const std::string& name, SchemaSensorTypeUtility::SensorType type);
        SchemaSensorBasic(const std::string& name, SchemaSensorTypeUtility::SensorType type, float value);
        SchemaSensorBasic(const std::string& name, SchemaSensorTypeUtility::SensorType type, const std::string& value);
        SchemaSensorBasic(const std::string& name, SchemaSensorTypeUtility::SensorType type, float value, const std::string& unite);
        SchemaSensorBasic(const std::string& name, SchemaSensorTypeUtility::SensorType type, const std::string& value, const std::string& unite);

        /// \brief    Destructor of SensorBasic
        ~SchemaSensorBasic();

        void SetDevice(const std::string& name, SchemaSensorTypeUtility::SensorType type);
        void SetDeviceName(const std::string& name);
        void SetDeviceType(SchemaSensorTypeUtility::SensorType type);
        void SetCurrent(float value);
        void SetCurrent(const std::string& value);
        void SetUnit(const std::string& unit);
        float GetCurrent();

        void Check();

    private:
};

/// \brief    Class to manage schema sensor.request xPL message
class SchemaSensorRequest : public SchemaObject
{
    public:
        /// \brief    Constructor of SensorBasic
        SchemaSensorRequest();

        /// \brief    Destructor of SensorBasic
        ~SchemaSensorRequest();

        void SetDevice(const std::string& name, SchemaSensorTypeUtility::SensorType type);
        void SetDeviceName(const std::string& name);
        void SetDeviceType(SchemaSensorTypeUtility::SensorType type);
};

}
#endif // SCHEMASENSORBASIC_H
