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

#ifndef XPLADVCONFIG_H
#define XPLADVCONFIG_H

#include "../xPLDevice.h"
#include "../Schemas/SchemaAdvanceConfig.h"

namespace xPL
{

class AdvanceConfig : public xPLDevice::IExtensionConfig
{
    public:
        class ICallBack;
        class AdvanceFormat;
        enum ParamType {STRING, INTEGER, FLOAT, BOOLEAN, DEVICE};
        enum ParamList {NONE, SENSORTYPE, CONTROLTYPE, MODULE};
        AdvanceConfig(xPLDevice* xPLDev);
        ~AdvanceConfig();

        bool MsgAnswer(SchemaObject& msg);
        void LoadConfig(SimpleIni& iniFile);
        void SaveConfig(SimpleIni& iniFile);
        void SetCallBack(ICallBack* callBack);

        void AddFormat(const std::string& name, ParamType paramType, ParamList paramList);
        void AddConfig(std::map<std::string, std::string> values);
        int GetNbConfig();
        std::map<std::string, std::string>* GetConfig(unsigned int nb);
        std::map<std::string, std::string>* GetConfig(const std::string& name);
        bool DelConfig(unsigned int nb);
        bool DelConfig(const std::string& name);
        void DelAllConfig();
        SchemaAdvanceConfigList ToFormatMessage();
        SchemaAdvanceConfigCurrent ToConfigMessage(unsigned int nb);
        SchemaAdvanceConfigCurrent ToConfigMessage(const std::string& name);

    private:
        SimpleLog* m_Log;
        xPLDevice* m_xPLDevice;
        ICallBack* m_CallBack;
        std::string m_ConfigKey;

        int NameToNb(const std::string& name);
        std::string ToString(ParamType paramType);
        std::string ToString(ParamList paramList);
        std::map<std::string, AdvanceFormat*> m_AdvanceFormats;
        std::vector<std::map<std::string, std::string>> m_DeviceValues;
};

class AdvanceConfig::ICallBack
{
    public:
        virtual void AdvanceConfigure() = 0;
        virtual void ConfigChange(const std::string& name) = 0;
        virtual void ConfigDelete(const std::string& name) = 0;
        virtual void ConfigsDelete() = 0;
};

class AdvanceConfig::AdvanceFormat
{
    public:
        ParamType paramType;
        ParamList paramList;
};

}
#endif // XPLADVCONFIG_H
