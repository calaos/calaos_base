/*** LICENCE ***************************************************************************************/
/*
  xPPLib - Simple class to manage xPL or xAP protocol

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

#ifndef XPL_DEVCFG_H
#define XPL_DEVCFG_H

#include "xPLDevice.h"
#include "Extensions/BasicConfig.h"

namespace xPL
{

/// \brief    Very simple class to manage configuration files
/// \details  Class allows you to easily manage configuration files with less than 10 methods.
class xPLDevCfg : public xPLDevice, public BasicConfig::IConfigure
{
    public:
        xPLDevCfg();
        xPLDevCfg(const std::string& vendor, const std::string& device);
        xPLDevCfg(const std::string& vendor, const std::string& device, const std::string& instance);

        /// \brief    Destructor of xPPDevice
        /// \details  Destructor of xPPDevice, deallocate memory, like Free method.
        ~xPLDevCfg();

        void Init();
        void Configure();

        void AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, const std::string& value);
        void AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, int nb);
        ConfigItem* GetConfigItem(const std::string& name);
        void SetCallBackConfig(BasicConfig::IConfigure* callBackConfig);

    private:
        BasicConfig m_BasicConfig;
        BasicConfig::IConfigure* m_CallBackConfig;
};

}
#endif // XPL_DEVICE_H
