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

#include <cstdlib>
#include "xPLDevCfg.h"

namespace xPL
{

using namespace std;

/**************************************************************************************************************/
/***                                                                                                        ***/
/*** Class xPLDevCfg                                                                                           ***/
/***                                                                                                        ***/
/**************************************************************************************************************/
xPLDevCfg::xPLDevCfg() : xPLDevice(), m_BasicConfig(this)
{
    Init();
}

xPLDevCfg::xPLDevCfg(const string& vendor, const string& device) : xPLDevice(vendor, device), m_BasicConfig(this)
{
    Init();
}

xPLDevCfg::xPLDevCfg(const string& vendor, const string& device, const string& instance) : xPLDevice(vendor, device, instance), m_BasicConfig(this)
{
    Init();
}

xPLDevCfg::~xPLDevCfg()
{
}

void xPLDevCfg::Init()
{
    m_CallBackConfig = nullptr;
    AddExtension(&m_BasicConfig);
    AddBasicConfig("newconf", ConfigItem::Reconf, GetInstance());
    AddBasicConfig("interval", ConfigItem::Reconf, "5");
    #ifdef DEBUG
    AddBasicConfig("logdestination", ConfigItem::Reconf, "cout");
    AddBasicConfig("loglevel", ConfigItem::Reconf, "5");
    #else
    AddBasicConfig("logdestination", ConfigItem::Reconf, "cerr");
    AddBasicConfig("loglevel", ConfigItem::Reconf, "2");
    #endif // DEBUG
    AddBasicConfig("logmodule", ConfigItem::Option, "");
    AddBasicConfig("logfunction", ConfigItem::Option, "");
    AddBasicConfig("group", ConfigItem::Option, 16);
    AddBasicConfig("filter", ConfigItem::Option, 16);
    AddBasicConfig("network", ConfigItem::Config, "");
    m_BasicConfig.SetCallBackConfig(this);
}

void xPLDevCfg::Configure()
{
    ConfigItem* pConfigItem;
    int ival;
    string value;


    switch(GetHeartBeatType())
    {
        case ConfigBASIC :
            SetHeartBeatType(HeartBeatBASIC);
            break;
        case ConfigAPP :
            SetHeartBeatType(HeartBeatAPP);
            break;
        default :
            break;
    }

    ///Hbeat interval
	pConfigItem = GetConfigItem("interval");
	if(pConfigItem != nullptr)
    {
        value = pConfigItem->GetValue();
	    if(value!="") SetHeartBeatInterval(atoi(value.c_str()));
    }

    ///Instance
	pConfigItem = GetConfigItem("newconf");
	if(pConfigItem != nullptr)
	{
        value = pConfigItem->GetValue();
        if(value!="") SetInstance(value);
	}

    ///Log level
	pConfigItem = GetConfigItem("loglevel");
	if(pConfigItem != nullptr)
    {
        ival = atoi(pConfigItem->GetValue().c_str());
        SetLogLevel(ival);
    }

    ///Log module
	pConfigItem = GetConfigItem("logmodule");
	if(pConfigItem != nullptr)
    {
        value = pConfigItem->GetValue();
        if(value!="") SetLogModule(value);
    }

    ///Log fonction
	pConfigItem = GetConfigItem("logfunction");
	if(pConfigItem != nullptr)
    {
        value = pConfigItem->GetValue();
        if(value!="") SetLogFunction(value);
    }

    ///Log destination
	pConfigItem = GetConfigItem("logdestination");
	if(pConfigItem != nullptr)
    {
        value = pConfigItem->GetValue();
        if(value!="") SetLogDestination(value);
    }

    ///Network interface for Linux or IpAddress for Windows
	pConfigItem = GetConfigItem("network");
	if(pConfigItem != nullptr)
    {
        value = pConfigItem->GetValue();
        if(value!="") SetNetworkInterface(value);
    }

    if(m_CallBackConfig != nullptr) m_CallBackConfig->Configure();
}

void xPLDevCfg::AddBasicConfig(const string& name, ConfigItem::ConfigItemType type, const string& value)
{
    m_BasicConfig.AddBasicConfig(name, type, value);
}

void xPLDevCfg::AddBasicConfig(const string& name, ConfigItem::ConfigItemType type, int nb)
{
    m_BasicConfig.AddBasicConfig(name, type, nb);
}

ConfigItem* xPLDevCfg::GetConfigItem(const string& name)
{
    return m_BasicConfig.GetConfigItem(name);
}

void xPLDevCfg::SetCallBackConfig(BasicConfig::IConfigure* callBackConfig)
{
    m_CallBackConfig = callBackConfig;
}

}
