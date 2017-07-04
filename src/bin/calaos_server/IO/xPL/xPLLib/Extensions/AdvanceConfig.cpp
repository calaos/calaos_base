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
#include "AdvanceConfig.h"
namespace xPL
{

using namespace std;

/**************************************************************************************************************/
/***                                                                                                        ***/
/*** Class AdvanceConfig                                                                                           ***/
/***                                                                                                        ***/
/**************************************************************************************************************/
AdvanceConfig::AdvanceConfig(xPLDevice* xPLDev)
{
    m_CallBack = nullptr;
    m_xPLDevice = xPLDev;
    m_Log = m_xPLDevice->GetLogHandle();
}

AdvanceConfig::~AdvanceConfig()
{
    map<string, AdvanceFormat*>::iterator it;


    for(it=m_AdvanceFormats.begin(); it!=m_AdvanceFormats.end(); ++it)
    {
        delete(it->second);
    }
    m_AdvanceFormats.clear();
}

void AdvanceConfig::SetCallBack(ICallBack* callBack)
{
    m_CallBack = callBack;
}

bool AdvanceConfig::MsgAnswer(SchemaObject& msg)
{
    if(msg.GetMsgType() != SchemaObject::cmnd)
    {
        LOG_VERBOSE(m_Log) << "Not a command message.";
        LOG_EXIT_OK;
        return false;
    }

    if(msg.GetClass() != "advanceconfig")
    {
        LOG_VERBOSE(m_Log) << "Not a advanceconfig class.";
        LOG_EXIT_OK;
        return false;
    }

    if(msg.GetType() == "list")
    {
        LOG_VERBOSE(m_Log) << "list type...";
        if(msg.GetValue("command") != "request")
        {
            LOG_VERBOSE(m_Log) << "but not command request";
            LOG_EXIT_OK;
            return false;
        }
        LOG_VERBOSE(m_Log) << "send advanceconfig.list message";
        SchemaAdvanceConfigList advanceConfigList;
        advanceConfigList = ToFormatMessage();
        m_xPLDevice->SendMessage(&advanceConfigList, msg.GetSource());
        LOG_EXIT_OK;
        return true;
    }

    if(msg.GetType() == "current")
    {
        string configName;
        SchemaAdvanceConfigCurrent advanceConfigCurrent;


        LOG_VERBOSE(m_Log) << "current type...";
        configName = msg.GetValue(m_ConfigKey);


        if(msg.GetValue("command") == "delete")
        {
            LOG_VERBOSE(m_Log) << "command delete...";
            if(configName=="")
            {
                LOG_VERBOSE(m_Log) << "all advanceConfigs";
                DelAllConfig();
                if(m_CallBack!=nullptr) m_CallBack->ConfigsDelete();
                m_xPLDevice->SaveConfig();
            }
            else
            {
                LOG_VERBOSE(m_Log) << "configName " << configName;
                DelConfig(configName);
                if(m_CallBack!=nullptr) m_CallBack->ConfigDelete(configName);
                m_xPLDevice->SaveConfig();
            }
            LOG_EXIT_OK;
            return true;
        }

        if(msg.GetValue("command") != "request")
        {
            LOG_VERBOSE(m_Log) << "but not command request";
            LOG_EXIT_OK;
            return false;
        }

        if(configName=="")
        {
            int nb,i;

            LOG_VERBOSE(m_Log) << "no device so return all devices";

            nb = GetNbConfig();
            for(i=0; i<nb; i++)
            {
                advanceConfigCurrent = ToConfigMessage(i);
                m_xPLDevice->SendMessage(&advanceConfigCurrent, msg.GetSource());
            }

            LOG_EXIT_OK;
            return true;
        }

        advanceConfigCurrent = ToConfigMessage(configName);
        if(advanceConfigCurrent.GetValue(m_ConfigKey)=="")
        {
            LOG_VERBOSE(m_Log) << "but configName '"<<configName<<"' not found";
            LOG_EXIT_OK;
            return false;
        }

        LOG_VERBOSE(m_Log) << "send advanceConfigCurrent stat message";
        m_xPLDevice->SendMessage(&advanceConfigCurrent, msg.GetSource());
        LOG_EXIT_OK;
        return true;
    }

    if(msg.GetType() == "request")
    {
        string configName;
        map<string, string> values;
        vector<xPL::SchemaObject::SchemaItem>::iterator itItem;
        unsigned nb, i;

        LOG_VERBOSE(m_Log) << "request type...";

        configName = msg.GetValue(m_ConfigKey);
        if(configName=="")
        {
            LOG_VERBOSE(m_Log) << "but no configName";
            LOG_EXIT_OK;
            return false;
        }

        for(itItem=msg.begin(); itItem!=msg.end(); ++itItem)
        {
            nb = itItem->Count();
            for(i=0; i<nb; i++)
                values[itItem->GetKey()] += itItem->GetValue(i);
        }

        AddConfig(values);
        if(m_CallBack!=nullptr) m_CallBack->ConfigChange(configName);
        m_xPLDevice->SaveConfig();
        LOG_EXIT_OK;
        return true;
    }

    LOG_VERBOSE(m_Log) << "Not a Current|List|Request type";
    LOG_EXIT_OK;
    return false;
}

void AdvanceConfig::LoadConfig(SimpleIni& iniFile)
{
    SimpleIni::SectionIterator sectIt;
    SimpleIni::KeyIterator keyIt;
    map<string, string> keyValuePair;


    for(sectIt=iniFile.beginSection(); sectIt!=iniFile.endSection(); ++sectIt)
    {
        if(*sectIt=="xPL") continue;
        keyValuePair.clear();

        keyValuePair[m_ConfigKey] = *sectIt;
        for(keyIt=iniFile.beginKey(*sectIt); keyIt!=iniFile.endKey(*sectIt); ++keyIt)
            keyValuePair[*keyIt] = iniFile.GetValue(*sectIt, *keyIt, "");
        AddConfig(keyValuePair);
    }
    if(m_CallBack!=nullptr) m_CallBack->AdvanceConfigure();
}

void AdvanceConfig::SaveConfig(SimpleIni& iniFile)
{
    int i;
    int nb;
    map<string, string>* keyValuePair;
    map<string, string>::iterator itKeyValue;


    nb = GetNbConfig();
    for(i=0; i<nb; i++)
    {
        keyValuePair = GetConfig(i);
        for(itKeyValue=keyValuePair->begin(); itKeyValue!=keyValuePair->end(); ++itKeyValue)
        {
            if(itKeyValue->first == m_ConfigKey) continue;
            iniFile.SetValue((*keyValuePair)[m_ConfigKey], itKeyValue->first, itKeyValue->second);
        }
    }
}

void AdvanceConfig::AddFormat(const string& name, ParamType paramType, ParamList paramList)
{
    AdvanceFormat* format;

    if(m_ConfigKey=="") m_ConfigKey = name;
    format = new AdvanceFormat();
    format->paramType = paramType;
    format->paramList = paramList;
    m_AdvanceFormats[name] = format;
}

void AdvanceConfig::AddConfig(map<string, string> values)
{
    int nb;

    nb = NameToNb(values[m_ConfigKey]);
    if(nb<0)
        m_DeviceValues.push_back(values);
    else
        m_DeviceValues[nb] = values;
}

int AdvanceConfig::GetNbConfig()
{
    return m_DeviceValues.size();
}

map<string, string>* AdvanceConfig::GetConfig(unsigned int nb)
{
    if(nb>=m_DeviceValues.size()) return nullptr;
    return &m_DeviceValues[nb];
}

map<string, string>* AdvanceConfig::GetConfig(const string& name)
{
    int nb;

    nb = NameToNb(name);
    if(nb<0) return nullptr;
    return &m_DeviceValues[nb];
}

bool AdvanceConfig::DelConfig(unsigned int nb)
{
    if(nb>=m_DeviceValues.size()) return false;
    m_DeviceValues.erase(m_DeviceValues.begin() + nb - 1);
    return true;
}

bool AdvanceConfig::DelConfig(const std::string& name)
{
    int nb;

    nb = NameToNb(name);
    if(nb<0) return false;
    m_DeviceValues.erase(m_DeviceValues.begin() + nb);
    return true;
}

void AdvanceConfig::DelAllConfig()
{
    m_DeviceValues.clear();
}

int AdvanceConfig::NameToNb(const string& name)
{
    int nb;
    int i;


    nb = m_DeviceValues.size();
    for(i=0; i<nb; i++)
        if(m_DeviceValues[i][m_ConfigKey]==name) return i;

    return -1;
}

string AdvanceConfig::ToString(ParamType paramType)
{
    switch(paramType)
    {
        case ParamType::STRING :
            return "string";
        case ParamType::INTEGER :
            return "integer";
        case ParamType::FLOAT :
            return "float";
        case ParamType::BOOLEAN :
            return "boolean";
        case ParamType::DEVICE :
            return "device";
    }

    return "unknown";
}

string AdvanceConfig::ToString(ParamList paramList)
{
    switch(paramList)
    {
        case ParamList::NONE :
            return "none";
        case ParamList::SENSORTYPE :
            return "sensortype";
        case ParamList::CONTROLTYPE :
            return "controltype";
        case ParamList::MODULE :
            return "module";
    }

    return "unknown";
}

SchemaAdvanceConfigList AdvanceConfig::ToFormatMessage()
{
    map<string, AdvanceFormat*>::iterator it;
    SchemaAdvanceConfigList msg;
    string value;


    for(it=m_AdvanceFormats.begin(); it!=m_AdvanceFormats.end(); ++it)
    {
        value = ToString(it->second->paramType);
        if(it->second->paramList!=ParamList::NONE)
            value += "["+ToString(it->second->paramList)+"]";
        msg.AddValue(it->first, value);
    }

    return msg;
}

SchemaAdvanceConfigCurrent AdvanceConfig::ToConfigMessage(unsigned int nb)
{
    map<string, string>::iterator it;
    SchemaAdvanceConfigCurrent msg;
    string value;


    if(nb>=m_DeviceValues.size()) return msg;

    for(it=m_DeviceValues[nb].begin(); it!=m_DeviceValues[nb].end(); ++it)
    {
        value = it->second;
        while(value!="")
        {
            msg.AddValue(it->first, value.substr(0, 127));
            if(value.length()>127)
                value = value.substr(127);
            else
                value = "";
        }
    }

    return msg;
}

SchemaAdvanceConfigCurrent AdvanceConfig::ToConfigMessage(const string& name)
{
    int nb;

    nb = NameToNb(name);
    return ToConfigMessage(nb);
}

}
