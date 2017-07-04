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

#include <iostream>
#include "SimpleFolders/SimpleFolders.h"
#include "xPLDevice.h"
#include "Schemas/SchemaConfig.h"

namespace xPL
{

using namespace std;

/**************************************************************************************************************/
/***                                                                                                        ***/
/*** Class xPLDevice                                                                                           ***/
/***                                                                                                        ***/
/**************************************************************************************************************/
xPLDevice::xPLDevice()
{
    Initialisation("", "", "");
}

xPLDevice::xPLDevice(const string& vendor, const string& device)
{
	 Initialisation(vendor, device, "default");
}

xPLDevice::xPLDevice(const string& vendor, const string& device, const string& instance)
{
	 Initialisation(vendor, device, instance);
}

void xPLDevice::Initialisation(const std::string& vendor, const std::string& device, const std::string& instance)
{
    vector<IExtension *>::iterator it;


    m_bAnswerAllMsg = false;
    m_bLoadConfig = false;
    m_logFile = "";
    m_Log = &m_SimpleLog;
    m_SimpleLog.SetFilter(&m_logFilter);
    m_SimpleLog.SetWriter(&m_logWriter);

    LOG_ENTER;

    if(vendor!="")
    {
        try
        {
            m_Source.SetAddress(vendor, device, instance);
        }
        catch(const Address::Exception &e)
        {
            LOG_FATAL(m_Log) << e.what();
            LOG_EXIT_KO;
            throw;
        }

        LOG_INFO(m_Log) << "Source : " << m_Source.ToString();
    }
	m_HBeatMsg = nullptr;
    m_HBeatType = ConfigAPP;
    m_HBeatInterval = 5;
    m_ConfigFile = "";

	LOG_EXIT_OK;
}

xPLDevice::~xPLDevice()
{
    if(m_HBeatMsg!=nullptr) delete m_HBeatMsg;
}

void xPLDevice::AddExtension(IExtension* extensionClass)
{
    m_ExtensionClass.push_back(extensionClass);
}

void xPLDevice::AddExtension(IExtensionConfig* extensionClass)
{
    m_ExtensionConfigClass.push_back(extensionClass);
}

SimpleLog* xPLDevice::GetLogHandle()
{
    return m_Log;
}

void xPLDevice::SetAppName(const string& appName, const string& appVersion)
{
    m_AppName = appName;
    m_AppVersion = appVersion;
}

void xPLDevice::SetNetworkInterface(const std::string& networkInterface)
{
    m_networkInterface = networkInterface;
}

void xPLDevice::SetInstance(const string& instance)
{
    LOG_ENTER;

    if(instance==m_Source.GetInstance())
    {
        LOG_VERBOSE(m_Log) << "L'instance n'a pas changé";
        LOG_EXIT_OK;
        return;
    }

    if(m_SenderSock.isOpen()) SendHeartBeatEnd();
    try
    {
        m_Source.SetAddress(m_Source.GetVendor(), m_Source.GetDevice(), instance);
    }
    catch(const Address::Exception &e)
    {
        LOG_FATAL(m_Log) << e.what();
        LOG_EXIT_KO;
        throw;
    }
    if(m_SenderSock.isOpen()) SendMessage(m_HBeatMsg, "*");

    LOG_INFO(m_Log) << "Source : " << m_Source.ToString();
    LOG_EXIT_OK;
}

string xPLDevice::GetInstance()
{
    return m_Source.GetInstance();
}

void xPLDevice::SetHeartBeatInterval(int interval)
{
    SetHeartBeat(m_HBeatType, interval);
}

void xPLDevice::SetHeartBeatType(HeartBeatType type)
{
    SetHeartBeat(type, m_HBeatInterval);
}

xPLDevice::HeartBeatType xPLDevice::GetHeartBeatType()
{
    return m_HBeatType;
}

void xPLDevice::SetLogLevel(int level)
{
    LOG_VERBOSE(m_Log) << "Log Level is set to " << level;
    switch(level)
    {
        case 1 : m_logFilter.SetLevel(SimpleLog::LVL_FATAL); break;
        case 2 : m_logFilter.SetLevel(SimpleLog::LVL_ERROR); break;
        case 3 : m_logFilter.SetLevel(SimpleLog::LVL_WARNING); break;
        case 4 : m_logFilter.SetLevel(SimpleLog::LVL_INFO); break;
        case 5 : m_logFilter.SetLevel(SimpleLog::LVL_DEBUG); break;
        case 6 : m_logFilter.SetLevel(SimpleLog::LVL_VERBOSE); break;
        case 7 : m_logFilter.SetLevel(SimpleLog::LVL_TRACE); break;
        default : m_logFilter.SetLevel(SimpleLog::LVL_WARNING); break;
    }
}

void xPLDevice::SetLogModule(const string& module)
{
    LOG_VERBOSE(m_Log) << "Log Module is set to " << module;
    m_logFilter.SetModule(module);
}

void xPLDevice::SetLogFunction(const std::string& funct)
{
    LOG_VERBOSE(m_Log) << "Log Function is set to " << funct;
    m_logFilter.SetFunction(funct);
}

void xPLDevice::SetLogDestination(const std::string& value)
{
    if(value == m_logFile) return;

    LOG_VERBOSE(m_Log) << "Log Destination is set to " << value;

    if((value=="cout")||(value==""))
        m_logWriter.SetStream(cout);
    else if(value=="cerr")
        m_logWriter.SetStream(cerr);
    else if(value=="clog")
        m_logWriter.SetStream(clog);
    else
    {
        m_logStream.open(value, ios_base::app);
        m_logWriter.SetStream(m_logStream);
    }
    m_logFile = value;
}

void xPLDevice::SetGroups(const std::vector<std::string>& group)
{
    std::vector<std::string>::const_iterator it;

    m_Groups.clear();
    for(it=group.begin(); it!=group.end(); ++it)
        m_Groups.push_back(*it);
}

void xPLDevice::SetFilters(const std::vector<std::string>& filter)
{
    std::vector<std::string>::const_iterator it;

    m_Filters.clear();
    for(it=filter.begin(); it!=filter.end(); ++it)
        m_Filters.push_back(*it);
}

void xPLDevice::SetHeartBeat(HeartBeatType type, int interval)
{
	LOG_ENTER;

    LOG_INFO(m_Log) << "Interval : " << interval;

    if(interval<5)
    {
        interval = 5;
        LOG_VERBOSE(m_Log) << "Interval adjusts to : " << interval;
    }
    if(interval>30)
    {
        interval = 30;
        LOG_VERBOSE(m_Log) << "Interval adjusts to : " << interval;
    }

    if(m_HBeatMsg!=nullptr) delete m_HBeatMsg;

    m_HBeatType = type;
    m_HBeatInterval = interval;

    switch(type)
    {
        case xPLDevice::ConfigBASIC :
            m_HBeatMsg = new SchemaConfigBasic(interval);
            break;

        case xPLDevice::ConfigAPP :
            m_HBeatMsg = new SchemaConfigApp(interval, m_ReceiverSock.GetPort(), m_ReceiverSock.LocalAddress(""));
            break;

        case xPLDevice::HeartBeatBASIC :
            m_HBeatMsg = new SchemaHbeatBasic(interval);
            break;

        case xPLDevice::HeartBeatAPP :
            m_HBeatMsg = new SchemaHbeatApp(interval, m_ReceiverSock.GetPort(), m_ReceiverSock.LocalAddress(""));
            break;

        default :
            LOG_WARNING(m_Log) << "Unknown HeartBeatType";
    }

    if(m_HBeatMsg==nullptr)
    {
        m_HBeatMsg = new SchemaHbeatBasic(15);
        LOG_DEBUG(m_Log) << "Rescue : HeartBeat fix to hbeat.basic, 15 secondes";
    }

    if(m_AppName != "") m_HBeatMsg->AddValue("appname", m_AppName);
    if(m_AppVersion != "") m_HBeatMsg->AddValue("version", m_AppVersion);

    LOG_EXIT_OK;
}

unsigned short xPLDevice::GetTCPPort()
{
    return m_ReceiverSock.GetPort();
}

void xPLDevice::DiscoverTCPPort()
{
    int portHub = 3865;
    int portMin = 49152;
    int portMax = 65535;
    int portTCP;

	LOG_ENTER;
    m_ReceiverSock.SetNetworkInterface(m_networkInterface);
    m_ReceiverSock.Blocking(false);

    try
    {
        m_ReceiverSock.Listen(portHub, INADDR_ANY);
        LOG_INFO(m_Log) << "Discovered port " << portHub;
        LOG_VERBOSE(m_Log) << "Assuming no local hub is active";
        LOG_EXIT_OK;
        return;
    }
    catch(const SimpleSockUDP::Exception &e)
    {
        if(e.GetNumber() != 0x0025) throw;
        LOG_VERBOSE(m_Log) << "Socket port " << portHub << " in use" ;
        LOG_VERBOSE(m_Log) << "Assuming a hub is active";
    }

    for(portTCP=portMin; portTCP <= portMax; portTCP++)
    {
        try
        {
            m_ReceiverSock.Listen(portTCP, INADDR_ANY); //"127.0.0.1"); //"192.168.0.84"); //INADDR_ANY);
            LOG_INFO(m_Log) << "Discovered port " << portTCP;
            LOG_EXIT_OK;
            return;
        }
        catch(const SimpleSockUDP::Exception &e)
        {
            if(e.GetNumber() != 0x0025) throw;
            LOG_VERBOSE(m_Log) << "Socket port " << portTCP << " in use";
        }
    }

    LOG_FATAL(m_Log) << "No TCP port available";
    LOG_EXIT_KO;
    throw xPLDevice::Exception(0x0202, "xPLDevice::DiscoverTCPPort: No TCP port available");
    return;
}

void xPLDevice::SendMessage(ISchema *Schema, const string& target)
{
    string strMsg;

	LOG_ENTER;

    try
    {
        Schema->Check();
    }
    catch(const char * errMsg)
    {
        LOG_WARNING(m_Log) << errMsg;
        LOG_EXIT_KO;
        return;
    }

    strMsg = Schema->ToMessage(m_Source.ToString(), target);

    if(!m_SenderSock.isOpen())
    {
        LOG_VERBOSE(m_Log) << "Socket not open, message transfered in the cache";
        m_PreSend.push_back(strMsg);
        LOG_EXIT_OK;
        return;
    }

    LOG_VERBOSE(m_Log) << "Send message : " << strMsg;
    m_SenderSock.Send(strMsg);

    LOG_EXIT_OK;
}

void xPLDevice::SendHeartBeat()
{
    time_t timeNow;
    static time_t timeLast=0;
    int interval;

	//LOG_ENTER;
    if((m_HBeatType==xPLDevice::ConfigBASIC)||(m_HBeatType==xPLDevice::ConfigAPP))
        interval = 3;
    else
        interval = m_HBeatInterval*60;

    timeNow = time((time_t*)0);
    if((timeNow-timeLast>=interval)||(timeLast==0))
    {
 		timeLast=timeNow;
        SendMessage(m_HBeatMsg, "*");
	}

    //LOG_EXIT_OK;
}

void xPLDevice::SendHeartBeatEnd()
{
    SchemaObject *hbeatMsg;
	LOG_ENTER;

    hbeatMsg = new SchemaHbeatEnd();
    SendMessage(hbeatMsg, "*");
    delete hbeatMsg;

    LOG_EXIT_OK;
}

void xPLDevice::Open()
{
    vector<string>::iterator it;
    int nb;
	LOG_ENTER;

    if(m_bLoadConfig==false) LoadConfig();

    DiscoverTCPPort();
    m_SenderSock.Open(3865);

    if((m_HBeatType==xPLDevice::ConfigAPP)||(m_HBeatType==xPLDevice::HeartBeatAPP)) SetHeartBeat(m_HBeatType, m_HBeatInterval);
    SendHeartBeat();

    nb = m_PreSend.size();
    if(nb>0)
    {
        LOG_VERBOSE(m_Log) << nb << " messages to send.";

        for(it=m_PreSend.begin(); it!=m_PreSend.end(); ++it)
            m_SenderSock.Send(*it);

        m_PreSend.clear();
    }

    LOG_EXIT_OK;
}

void xPLDevice::Close()
{
	LOG_ENTER;

    SendHeartBeatEnd();
    m_SenderSock.Close();
    m_ReceiverSock.Close();

    m_bLoadConfig = false;

    LOG_EXIT_OK;
}

void xPLDevice::SetAnswerAllMsg(bool bAnswerAllMsg)
{
    m_bAnswerAllMsg = bAnswerAllMsg;
}

bool xPLDevice::WaitRecv(int delay)
{
    vector<IExtension *>::iterator it;
    vector<IExtensionConfig *>::iterator itConfig;
    bool bCallExtention;
    string xPLRaw;
    SchemaObject xPLparse;


    SendHeartBeat();

    if(!m_ReceiverSock.WaitRecv(delay))
    {
        LOG_TRACE(m_Log) << "Nothing to receive";
        return false;
    }
    if(!m_ReceiverSock.Recv(xPLRaw))
    {
        LOG_VERBOSE(m_Log) << "Failed to receive";
        return false;
    }

    LOG_VERBOSE(m_Log) << "Receive message : " << xPLRaw;
    while(xPLRaw!="")
    {
        try
        {
            xPLRaw = xPLparse.Parse(xPLRaw);
        }
        catch(const SchemaObject::Exception &e)
        {
            LOG_VERBOSE(m_Log) << "Failed to parse : " << e.what();
            return false;
        }
        bCallExtention = m_bAnswerAllMsg;
        if(MsgForMe(xPLparse))
        {
            if(!MsgAnswer(xPLparse)) bCallExtention = true;
        }
        if(bCallExtention==true)
        {
            for(it=m_ExtensionClass.begin(); it!=m_ExtensionClass.end(); ++it)
            {
                LOG_VERBOSE(m_Log) << "Call extended message answer";
                (*it)->MsgAnswer(xPLparse);
            }
            for(itConfig=m_ExtensionConfigClass.begin(); itConfig!=m_ExtensionConfigClass.end(); ++itConfig)
            {
                LOG_VERBOSE(m_Log) << "Call extended message answer";
                (*itConfig)->MsgAnswer(xPLparse);
            }
        }
    }
    return true;
}

bool xPLDevice::FilterAllow(const SchemaObject& msg)
{
    size_t posDeb, posFin;
    string filter;
    string filterPart;
    string msgType;
    vector<string>::const_iterator it;


	LOG_ENTER;

    msgType = msg.GetMsgTypeStr();

    if(m_Filters.size()==0)
    {
        LOG_VERBOSE(m_Log) << "No filters";
        LOG_EXIT_OK;
        return true;
    }
    for(it=m_Filters.begin(); it!=m_Filters.end(); ++it)
    {
        filter = *it;

        // Check the message type
        posFin = filter.find('.');
        if(posFin == string::npos) continue;
        filterPart = filter.substr(0, posFin);
        if((filterPart!="*")&&(filterPart!=msgType)) continue;

       	// Check the schema class
       	posDeb = posFin+1;
        posFin = filter.find('.', posDeb);
        filterPart = filter.substr(posDeb, posFin-posDeb+1);
        if((filterPart!="*")&&(filterPart!=msg.GetClass())) continue;

        // Check the schema type
       	posDeb = posFin+1;
        posFin = filter.find('.', posDeb);
        filterPart = filter.substr(posDeb, posFin-posDeb+1);
        if((filterPart!="*")&&(filterPart!=msg.GetType())) continue;

        // Check the vendor
       	posDeb = posFin+1;
        posFin = filter.find('.', posDeb);
        filterPart = filter.substr(posDeb, posFin-posDeb+1);
        if((filterPart!="*")&&(filterPart!=msg.TargetAddress.GetVendor())) continue;

        // Check the device
       	posDeb = posFin+1;
        posFin = filter.find('.', posDeb);
        filterPart = filter.substr(posDeb, posFin-posDeb+1);
        if((filterPart!="*")&&(filterPart!=msg.TargetAddress.GetDevice())) continue;

        // Check the instance
       	posDeb = posFin+1;
        posFin = filter.find('.', posDeb);
        filterPart = filter.substr(posDeb, posFin-posDeb+1);
        if((filterPart!="*")&&(filterPart!=msg.TargetAddress.GetInstance())) continue;

        LOG_VERBOSE(m_Log) << "One filter match";
        LOG_EXIT_OK;
        return true;
    }

    LOG_VERBOSE(m_Log) << "No filters match";
    LOG_EXIT_OK;
	return false;
}

bool xPLDevice::InGroup(const string& target)
{
    size_t pos;
    vector<string>::const_iterator it;


	LOG_ENTER;

    if(m_Groups.size()==0)
    {
        LOG_VERBOSE(m_Log) << "No groups";
        LOG_EXIT_OK;
        return false;
    }

    pos = target.find('.');
    if(pos==string::npos)
    {
        LOG_VERBOSE(m_Log) << "No point in target";
        LOG_EXIT_OK;
        return false;
    }
    if(target.substr(0, pos) != "xpl-group")
    {
        LOG_VERBOSE(m_Log) << "Target is not a group";
        LOG_EXIT_OK;
        return false;
    }

    for(it=m_Groups.begin(); it!=m_Groups.end(); ++it)
    {
        if(*it==target)
        {
            LOG_VERBOSE(m_Log) << "Target match with a group";
            LOG_EXIT_OK;
            return true;
        }
    }

    LOG_VERBOSE(m_Log) << "Target does not match with a group";
    LOG_EXIT_OK;
    return false;
}

bool xPLDevice::MsgForMe(SchemaObject& msg)
{
    string meSource = m_Source.ToString();
    string msgTarget = msg.TargetAddress.ToString();

	LOG_ENTER;
    LOG_VERBOSE(m_Log) << "The target " << msgTarget << " is for me ?";

    /*
    if(msg.GetSource() == meSource)
    {
        LOG_VERBOSE(m_Log) << "Not for me, i'am the sender.";
        LOG_EXIT_OK;
        return false;
    }
    */
    if(msgTarget == meSource)
    {
        LOG_VERBOSE(m_Log) << "For me, target match with my source.";
        LOG_EXIT_OK;
        return true;
    }

    if((msgTarget == "*") && (FilterAllow(msg)) )
    {
        LOG_VERBOSE(m_Log) << "For me, target all and filters allow.";
        LOG_EXIT_OK;
        return true;
    }

    if(InGroup(msgTarget))
    {
        LOG_VERBOSE(m_Log) << "For me, groups allow.";
        LOG_EXIT_OK;
        return true;
    }

    LOG_VERBOSE(m_Log) << "Not for me.";
    LOG_EXIT_OK;
    return false;
}

bool xPLDevice::MsgAnswer(SchemaObject& msg)
{
	LOG_ENTER;


    if(msg.GetMsgType() != SchemaObject::cmnd)
    {
        LOG_VERBOSE(m_Log) << "Not a command message.";
        LOG_EXIT_OK;
        return false;
    }

    if(msg.GetClass() == "hbeat")
    {
        LOG_VERBOSE(m_Log) << "hbeat class...";
        if(msg.GetType() != "request")
        {
            LOG_VERBOSE(m_Log) << "but not type request";
            LOG_EXIT_OK;
            return false;
        }
        if(msg.GetValue("command") != "request")
        {
            LOG_VERBOSE(m_Log) << "but not command request";
            LOG_EXIT_OK;
            return false;
        }
        LOG_VERBOSE(m_Log) << "send hbeat message";
        SendMessage(m_HBeatMsg, msg.GetSource());
        LOG_EXIT_OK;
        return true;
    }

    LOG_EXIT_OK;
	return false;
}

string xPLDevice::GetConfigFolder()
{
    string configFolder;

    configFolder = SimpleFolders::GetFolder(SimpleFolders::FolderType::User)+"."+m_Source.GetVendor();
    if(!SimpleFolders::FolderExists(configFolder))
    {
        try
        {
            SimpleFolders::CreateFolder(configFolder);
        }
        catch(const exception &e)
        {
            LOG_WARNING(m_Log) << e.what();
        }
    }

    return configFolder;
}

void xPLDevice::SetConfigFileName(const char* fileName)
{
    m_ConfigFile = string(fileName);
}

string xPLDevice::GetConfigFileName()
{
    if(m_ConfigFile!="") return m_ConfigFile;
    return SimpleFolders::AddFile(GetConfigFolder(), m_Source.GetDevice(), "conf");
}

bool xPLDevice::LoadConfig()
{
    string configFileName;
    SimpleIni iniFile;
    vector<IExtensionConfig *>::iterator it;


    configFileName = GetConfigFileName();
    LOG_INFO(m_Log) << "Load config (file " << configFileName << ")";

    iniFile.SetOptions(iniFile.Comment, "#");
    if(!iniFile.Load(configFileName))
        return false;

    m_bLoadConfig = true;

    for(it=m_ExtensionConfigClass.begin(); it!=m_ExtensionConfigClass.end(); ++it)
    {
        LOG_VERBOSE(m_Log) << "Call extended configuration";
        (*it)->LoadConfig(iniFile);
    }

    return true;
}

bool xPLDevice::SaveConfig()
{
    SimpleIni iniFile;
    vector<IExtensionConfig *>::iterator it;


    LOG_VERBOSE(m_Log) << "Save config";

    for(it=m_ExtensionConfigClass.begin(); it!=m_ExtensionConfigClass.end(); ++it)
    {
        LOG_VERBOSE(m_Log) << "Call extended configuration";
        (*it)->SaveConfig(iniFile);
    }

    iniFile.SaveAs(GetConfigFileName());
    return true;
}

bool xPLDevice::isDevice(const string& deviceName)
{
    size_t pos;

    pos = deviceName.find("-");
    if(pos==string::npos) return false;
    pos = deviceName.find(".", pos);
    if(pos==string::npos) return false;
    pos = deviceName.find(":", pos);
    if(pos==string::npos) return false;

    return true;
}

xPLDevice::Exception::Exception(int number, string const& message) throw()
{
    m_number = number;
    m_message = message;
}

xPLDevice::Exception::~Exception() throw()
{
}

const char* xPLDevice::Exception::what() const throw()
{
    return m_message.c_str();
}

int xPLDevice::Exception::GetNumber() const throw()
{
    return m_number;
}

}
