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

#include "BasicConfig.h"
namespace xPL
{

using namespace std;

/**************************************************************************************************************/
/***                                                                                                        ***/
/*** Class BasicConfig                                                                                           ***/
/***                                                                                                        ***/
/**************************************************************************************************************/
BasicConfig::BasicConfig(xPLDevice* xPLDev)
{
    m_CallBackConfig = nullptr;
    m_xPLDevice = xPLDev;
    m_Log = m_xPLDevice->GetLogHandle();
}

BasicConfig::~BasicConfig()
{
    vector<ConfigItem*>::iterator itConfig;


    for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end(); ++itConfig)
	{
		delete *itConfig;
	}
}

void BasicConfig::SetCallBackConfig(IConfigure* callBackConfig)
{
    m_CallBackConfig = callBackConfig;
}

void BasicConfig::AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, const std::string& value)
{
    ConfigItem *pConfigItem;


    pConfigItem = new xPL::ConfigItem(name, type);
	pConfigItem->AddValue(value);
	AddConfigItem(pConfigItem);
}

void BasicConfig::AddBasicConfig(const std::string& name, ConfigItem::ConfigItemType type, int nb)
{
    ConfigItem *pConfigItem;


    pConfigItem = new xPL::ConfigItem(name, type, nb);
	AddConfigItem(pConfigItem);
}

bool BasicConfig::MsgAnswer(SchemaObject& msg)
{
    if(msg.GetMsgType() != SchemaObject::cmnd)
    {
        LOG_VERBOSE(m_Log) << "Not a command message.";
        LOG_EXIT_OK;
        return false;
    }

    if(msg.GetClass() != "config")
    {
        LOG_VERBOSE(m_Log) << "Not a config class.";
        LOG_EXIT_OK;
        return false;
    }

    if(msg.GetType() == "current")
    {
        LOG_VERBOSE(m_Log) << "current type...";
        if(msg.GetValue("command") != "request")
        {
            LOG_VERBOSE(m_Log) << "but not command request";
            LOG_EXIT_OK;
            return false;
        }
        LOG_VERBOSE(m_Log) << "send config.current message";
        SchemaConfigCurrent configCurrent;
        configCurrent = ToConfigCurrent();
        m_xPLDevice->SendMessage(&configCurrent, msg.GetSource());
        LOG_EXIT_OK;
        return true;
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
        LOG_VERBOSE(m_Log) << "send config.list message";
        SchemaConfigList configList;
        configList = ToConfigList();
        m_xPLDevice->SendMessage(&configList, msg.GetSource());
        LOG_EXIT_OK;
        return true;
    }

    if(msg.GetType() == "response")
    {
        LOG_VERBOSE(m_Log) << "response type";
        size_t i;
        SchemaObject::SchemaItem* pSchemaItem;
        ConfigItem* pConfigItem;
        SchemaObject::SchemaItem::Iterator itValue;

        for(i=0; i<m_ConfigItems.size(); i++)
        {
            pConfigItem = m_ConfigItems[i];

            pSchemaItem = msg.GetItem(pConfigItem->GetName());
            if(pSchemaItem==nullptr) continue;

            pConfigItem->ClearValues();

            for(itValue=pSchemaItem->begin(); itValue!=pSchemaItem->end(); ++itValue)
                pConfigItem->AddValue(*itValue);
        }

        if(m_CallBackConfig != nullptr) m_CallBackConfig->Configure();
        m_xPLDevice->SaveConfig();
        LOG_EXIT_OK;
        return true;
    }

    LOG_VERBOSE(m_Log) << "Not a current|list|response type.";
    LOG_EXIT_OK;
    return false;
}

void BasicConfig::LoadConfig(SimpleIni& iniFile)
{
    int i;
    int nb;
    ostringstream name;
    string value;
    vector<ConfigItem*>::const_iterator itConfig;


    for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end();++itConfig)
    {
        (*itConfig)->ClearValues();
        nb = (*itConfig)->GetMaxValues();

        if(nb < 2)
        {
            (*itConfig)->AddValue(iniFile.GetValue("xPL", (*itConfig)->GetName(), ""));
            continue;
        }

        i = 0;
        do
        {
            i++;
            name.str("");
            name << (*itConfig)->GetName() << "[" << i << "]";
            value = iniFile.GetValue("xPL", name.str(), "");
            if(value!="") (*itConfig)->AddValue(value);
        } while(value!="");
    }

    if(m_CallBackConfig != nullptr) m_CallBackConfig->Configure();
}

void BasicConfig::SaveConfig(SimpleIni& iniFile)
{
    int i;
    int nb;
    ostringstream name;
    vector<ConfigItem*>::const_iterator itConfig;
    ConfigItem::Iterator itValue;


    for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end();++itConfig)
    {
        nb = (*itConfig)->GetMaxValues();
        if(nb<2)
        {
            iniFile.SetValue("xPL", (*itConfig)->GetName(), (*itConfig)->GetValue());
            continue;
        }
        i = 0;
        for(itValue=(*itConfig)->begin(); itValue!=(*itConfig)->end();++itValue)
        {
            i++;
            name.str("");
            name << (*itConfig)->GetName() << "[" << i << "]";
            iniFile.SetValue("xPL", name.str(), *itValue);
        }
    }
}

ConfigItem* BasicConfig::GetConfigItem(const string& name)
{
	vector<ConfigItem*>::iterator itConfig;

	LOG_ENTER;
	LOG_VERBOSE(m_Log) << "Search " << name << " config item";

	for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end(); ++itConfig)
	{
		if((*itConfig)->GetName() == name)
		{
            LOG_VERBOSE(m_Log) << "Item found";
		    LOG_EXIT_OK;
			return *itConfig;
		}
	}

    LOG_VERBOSE(m_Log) << "Item not found";
    LOG_EXIT_OK;
	return nullptr;
}

bool BasicConfig::RemoveConfigItem(const string& name)
{
	vector<ConfigItem*>::iterator itConfig;

	LOG_ENTER;

	for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end(); ++itConfig)
	{
		if((*itConfig)->GetName() == name)
		{
			delete *itConfig;
			m_ConfigItems.erase(itConfig);
		    LOG_EXIT_OK;
			return true;
		}
	}

    LOG_EXIT_OK;
	return false;
}

void BasicConfig::AddConfigItem(ConfigItem* pConfigItem)
{
	LOG_ENTER;

	if(GetConfigItem(pConfigItem->GetName())!=nullptr) RemoveConfigItem(pConfigItem->GetName());

    m_ConfigItems.push_back(pConfigItem);

    LOG_EXIT_OK;
}

SchemaConfigList BasicConfig::ToConfigList()
{
    SchemaConfigList configList;
    vector<ConfigItem*>::const_iterator itConfig;
    ostringstream value;
    size_t max;


    for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end();++itConfig)
    {
        value << (*itConfig)->GetName();

        max = (*itConfig)->GetMaxValues();
        if(max>1) value << "[" << max << "]";

        configList.AddValue(ConfigItemTypeToString((*itConfig)->GetType()), value.str());
        value.str("");
    }

    return configList;
}

string BasicConfig::ConfigItemTypeToString(ConfigItem::ConfigItemType type)
{
    switch(type)
    {
        case ConfigItem::ConfigItemType::Config : return "config";
        case ConfigItem::ConfigItemType::Option : return "option";
        case ConfigItem::ConfigItemType::Reconf : return "reconf";
        default : throw SchemaHbeatApp::Exception(0x0201, "BasicConfig::ConfigItemTypeToString : unknown type");
    }
}

SchemaConfigCurrent BasicConfig::ToConfigCurrent()
{
    SchemaConfigCurrent configCurrent;
    vector<ConfigItem*>::const_iterator itConfig;
    ConfigItem::Iterator itValue;


    for(itConfig=m_ConfigItems.begin(); itConfig!=m_ConfigItems.end();++itConfig)
    {
        if((*itConfig)->Count()>0)
        {
            for(itValue=(*itConfig)->begin(); itValue!=(*itConfig)->end();++itValue)
            {
                configCurrent.AddValue((*itConfig)->GetName(), *itValue);
            }
        }
        else
        {
            configCurrent.AddValue((*itConfig)->GetName(), "");
        }
    }

    return configCurrent;
}

/****************************************************************************************************/
/*** Class ConfigItem                                                                               */
ConfigItem::ConfigItem(const string& name, ConfigItemType type, size_t maxValues)
{
    m_Type = type;
    m_Name = name;
    m_MaxValues = maxValues;
}

ConfigItem::ConfigItem(const string& name, ConfigItemType type)
{
    m_Type = type;
    m_Name = name;
    m_MaxValues = 1;
}

ConfigItem::~ConfigItem()
{
    ClearValues();
}

void ConfigItem::ClearValues()
{
    m_Values.clear();
}

void ConfigItem::AddValue(const std::string& value)
{
    m_Values.push_back(value);
}

string ConfigItem::GetValue(size_t index)
{
	if(index >= m_Values.size() ) return "";
	return m_Values[index];
}

void ConfigItem::SetValue(size_t index, string value)
{
	if(index > m_Values.size() ) return;

	if(index == m_Values.size() )
        AddValue(value);
    else
        m_Values[index] = value;
}

void ConfigItem::SetValue(string value)
{
    SetValue(0, value);
}

string ConfigItem::GetName()
{
    return m_Name;
}

ConfigItem::ConfigItemType ConfigItem::GetType()
{
    return m_Type;
}

size_t ConfigItem::GetMaxValues()
{
    return m_MaxValues;
}

size_t ConfigItem::Count()
{
    return m_Values.size();
}

ConfigItem::Iterator ConfigItem::begin()
{
    return ConfigItem::Iterator(m_Values.begin());
}

ConfigItem::Iterator ConfigItem::end()
{
    return ConfigItem::Iterator(m_Values.end());
}

/****************************************************************************************************/
/*** Class Iterator                                                                                 */
ConfigItem::Iterator::Iterator()
{
}

ConfigItem::Iterator::Iterator(vector<string>::iterator vectorIterator)
{
    m_vectorIterator = vectorIterator;
}

const std::string& ConfigItem::Iterator::operator*()
{
    return *m_vectorIterator;
}

ConfigItem::Iterator ConfigItem::Iterator::operator++()
{
    ++m_vectorIterator;
    return *this;
}

bool ConfigItem::Iterator::operator==(Iterator const& a)
{
    return a.m_vectorIterator==m_vectorIterator;
}

bool ConfigItem::Iterator::operator!=(Iterator const& a)
{
    return a.m_vectorIterator!=m_vectorIterator;
}

}
