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

#ifndef XPLBASICCONFIG_H
#define XPLBASICCONFIG_H

#include "../xPLDevice.h"
#include "../Schemas/SchemaConfig.h"

namespace xPL
{

class ConfigItem
{
    public:
        enum ConfigItemType {Config, Reconf, Option};
        class Iterator;

        ConfigItem(const std::string& name, ConfigItemType type);
        ConfigItem(const std::string& name, ConfigItemType type, std::size_t maxValues);
        ~ConfigItem();

        void AddValue(const std::string& value);
        std::string GetValue(std::size_t index=0);
		template <class T> T GetValue(std::size_t index=0)
        {
            std::string value = GetValue(index);
            std::istringstream iss(value);
            T val;
            iss >> val;
            return val;
        }

        std::size_t GetMaxValues();
        std::size_t Count();
        void ClearValues();
        void SetValue(std::size_t index, std::string value);
        void SetValue(std::string value);

        std::string GetName();
        ConfigItemType GetType();

        ConfigItem::Iterator begin();
        ConfigItem::Iterator end();

    private:
        ConfigItemType m_Type;
        std::string m_Name;
        std::size_t m_MaxValues;
        std::vector<std::string> m_Values;
};

/// \brief    Iterator for ConfigItem class
/// \details  Class to browse values of a ConfigItem class.
class ConfigItem::Iterator
{
    public:
        /// \brief    Constructor of a iterator
        /// \details  Constructor to declare a iterator
        Iterator();
        /// \brief    Constructor of a iterator
        /// \details  Constructor to declare a iterator, call by ConfigItem::Begin
        Iterator(std::vector<std::string>::iterator vectorIterator);
        /// \brief    Overloading dereference operator
        /// \details  Overloading the dereference operator to get the values
        const std::string& operator*();
        /// \brief    Overloading pre-increment operator
        /// \details  Overloading the pre-increment operator to get the next value
        Iterator operator++();
        /// \brief    Overloading comparison operator ==
        /// \details  Overloading the comparison operator == to control the browse
        bool operator==(Iterator const& a);
        /// \brief    Overloading comparison operator !=
        /// \details  Overloading the comparison operator != to control the browse
        bool operator!=(Iterator const& a);

    private:
        std::vector<std::string>::iterator m_vectorIterator;
};

class BasicConfig : public xPLDevice::IExtensionConfig
{
    public:
        class IConfigure;

        BasicConfig(xPLDevice* xPLDev);
        ~BasicConfig();

        bool MsgAnswer(SchemaObject& msg);
        void LoadConfig(SimpleIni& iniFile);
        void SaveConfig(SimpleIni& iniFile);

        void SetCallBackConfig(IConfigure* callBackConfig);
        void AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, const std::string& value);
        void AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, int nb);

        ConfigItem* GetConfigItem(const std::string& name);
        bool RemoveConfigItem(const std::string& name);
        void AddConfigItem(ConfigItem* pConfigItem);

    private:
        SchemaConfigCurrent ToConfigCurrent();
        SchemaConfigList ToConfigList();
        std::string ConfigItemTypeToString(ConfigItem::ConfigItemType type);

        SimpleLog* m_Log;
        xPLDevice* m_xPLDevice;
        IConfigure* m_CallBackConfig;

        std::vector<ConfigItem*> m_ConfigItems;
};

class BasicConfig::IConfigure
{
    public:
        virtual void Configure() = 0;
};

}
#endif // XPLBASICCONFIG_H
