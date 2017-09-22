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

#ifndef SCHEMACONTROLBASIC_H
#define SCHEMACONTROLBASIC_H

#include <string>
#include "SchemaObject.h"

namespace xPL
{

/// \brief    Class to manage schema control.basic xPL message
class SchemaControlBasic : public SchemaObject
{
    public:
        enum controlType
        {
            manual = 0,
            unset,
            balance,
            flag,
            infrared,
            input,
            macro,
            mute,
            output,
            variable,
            periodic,
            scheduled,
            slider,
            timer
        };

        enum flagSet { set, clear, neutral };

        /// \brief    Constructor of ControlBasic
        SchemaControlBasic();
        SchemaControlBasic(std::string device, controlType type);
        SchemaControlBasic(std::string device, controlType type, std::string value);
        /// \brief    Constructor of ControlBasic
        virtual ~SchemaControlBasic();

        void SetDeviceName(std::string name);
        void SetDeviceType(controlType type);
        void SetDeviceType(std::string type);
        void SetCurrent(std::string current);
        std::string GetCurrent();

        void SetFlag(flagSet value);

        void Check();

        static controlType ToDeviceType(std::string type);

    protected:
    private:
        static const std::vector<std::string> m_TypeString;
        static const std::vector<std::string> m_FlagSet;
        controlType m_deviceType;
};
}
#endif // SCHEMACONTROLBASIC_H
