/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage socket communication TCP or UDP

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

#ifndef XPLSENSORS_H
#define XPLSENSORS_H

#include "../xPLDevice.h"
#include "../Schemas/SchemaSensor.h"

namespace xPL
{

class Sensors : public xPLDevice::IExtension
{
    public:
        Sensors(xPLDevice* xPLDev);
        ~Sensors();

        bool MsgAnswer(SchemaObject& msg);

        void AddMessage(SchemaObject* pMessage);
        void AddSensorMessage(std::string device, SchemaSensorTypeUtility::SensorType type, float value);
        SchemaObject* GetMessage(std::string device);
        bool RemoveMessage(std::string device);
        void RemoveAllMessages();
        bool ModifyMessage(std::string device, std::string value, std::string type="");

    private:
        SimpleLog* m_Log;
        xPLDevice* m_xPLDevice;
        std::vector<SchemaObject*> m_Messages;

};

}
#endif // XPLSENSORS_H
