/*** LICENCE ***************************************************************************************/
/*
  xPLLib - Simple class to manage xPL protocol

  This file is part of xPLLib.

    xPLLib is free software : you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    xPLLib is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with xPLLib.  If not, see <http://www.gnu.org/licenses/>.
*/
/***************************************************************************************************/

/*** MAIN PAGE FOR DOXYGEN *************************************************************************/
/// \mainpage xPLDevice Class Documentation
/// \section intro_sec Introduction
///
/// This class allows you to easily manage xPL protocol.\n
/// To use, include in your project xPLDevice.cpp and xPLDevice.h and ... (ToDo).
///
/// \section feature_sec Features
///
/// \li Send HearBeat message
/// \li Compile on Linux and Windows, Intel or ARM.
///
/// \section portability_sec Portability
/// Unit tests passed successfully on :
/// \li Windows Seven (CPU Intel Celeron)
/// \li Linux Ubuntu (CPU Intel Atom)
/// \li Linux Raspian on Raspberry Pi (CPU ARM)
/// \li Linux FunPlug on NAS DNS-320 (CPU ARM)\n
/// (Compilation directives define LINUX or WIN only necessary for colors in unit tests)
///
/// \section example_sec Example
/// See examples projets in Code::Blocks Workspace
///
/// \section licence_sec Licence
///  xPLLib is free software : you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.\n\n
///  xPLLib is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.\n\n
///  You should have received a copy of the GNU General Public License along with xPLLib. If not, see <http://www.gnu.org/licenses/>.
///
/***************************************************************************************************/

#ifndef XPL_DEVICE_H
#define XPL_DEVICE_H

#include <time.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <set>
#ifndef XPLLIB_NOSOCK
  #include "SimpleSock/SimpleSockUDP.h"
#endif
#ifndef XPLLIB_NOLOG
  #include "SimpleLog/SimpleLog.h"
  #define LOG_ENTER LOG_DEBUG(m_Log) << "*** Enter ***"
  #define LOG_EXIT_OK LOG_DEBUG(m_Log) << "*** Exit OK ***"
  #define LOG_EXIT_KO LOG_DEBUG(m_Log) << "*** Exit KO ***"
#else
    #define LOG_ENTER
    #define LOG_EXIT_OK
    #define LOG_EXIT_KO
    #define LOG_FATAL(log)  std::ostream(0)
    #define LOG_ERROR(log)  std::ostream(0)
    #define LOG_WARNING(log)    std::ostream(0)
    #define LOG_INFO(log)   std::ostream(0)
    #define LOG_VERBOSE(log)    std::ostream(0)
    #define LOG_DEBUG(log)  std::ostream(0)
    #define LOG_TRACE(log)  std::ostream(0)
#endif
#ifndef XPLLIB_NOCONF
  #include "SimpleFolders/SimpleFolders.h"
  #include "SimpleIni/SimpleIni.h"
#endif
#include "xPLLib/Address.h"
#include "xPLLib/Schemas/SchemaObject.h"
#include "xPLLib/Schemas/SchemaHbeat.h"


namespace xPL
{

/// \brief    Simple class to manage xPL protocol
/// \details  Class allows you to easily manage xPL protocol.
class xPLDevice
{
    public:
        class Exception;
        class IExtension;
        #ifdef XPLLIB_NOSOCK
        class ISockSend;
        #endif

        enum HeartBeatType {HeartBeatBASIC, HeartBeatAPP, ConfigBASIC, ConfigAPP};

        /// \brief    Constructor of xPLDevice
        /// \param    filename         Name of the configuration file.
        /// \details  Constructor of xPLDevice
        xPLDevice();
        xPLDevice(const std::string& vendor, const std::string& device);
        xPLDevice(const std::string& vendor, const std::string& device, const std::string& instance);
        void Initialisation(const std::string& vendor, const std::string& device, const std::string& instance);
        void AddExtension(IExtension* extensionClass);
        void SetAnswerAllMsg(bool bAllMsg);

        /// \brief    Destructor of xPPDevice
        /// \details  Destructor of xPPDevice, deallocate memory, like Free method.
        ~xPLDevice();

        void SetInstance(const std::string& instance);
        void SetAppName(const std::string& appName, const std::string& appVersion);
        void SetGroups(const std::set<std::string>& group);
        void SetFilters(const std::set<std::string>& filter);
        void SetHeartBeatInterval(int interval);
        void SetHeartBeatType(HeartBeatType type);
        bool MsgForMe(SchemaObject& msg);
        std::string GetInstance();
        HeartBeatType GetHeartBeatType();
        bool isDevice(const std::string& deviceName);
        bool MsgAnswer(SchemaObject& msg);
        void SendxPLMessage(ISchema *Schema, const std::string& target);
        unsigned short GetTCPPort();
        void Open();
        void Close();
        void SendHeartBeat(bool force);

        #ifndef XPLLIB_NOSOCK
          void SetNetworkInterface(const std::string& networkInterface);
          bool WaitRecv(int delay);
        #else
          void SetSendSockCallback(ISockSend *sockSend);
          void SetRecvSockInfo(const std::string& address, int port);
        #endif

        #ifndef XPLLIB_NOLOG
          SimpleLog* GetLogHandle();
          void SetLogLevel(int level);
          void SetLogModule(const std::string& module);
          void SetLogFunction(const std::string& funct);
          void SetLogDestination(const std::string& destination);
        #endif

        #ifndef XPLLIB_NOCONF
          class IExtensionConfig;
          void AddExtension(IExtensionConfig* extensionClass);
          std::string GetConfigFolder();
          bool LoadConfig();
          bool SaveConfig();
          void SetConfigFileName(const char* fileName);
        #endif

    private:
        bool FilterAllow(const SchemaObject& msg);
        bool InGroup(const std::string& target);
        void SetHeartBeat(HeartBeatType type, int interval);
        void SendHeartBeatEnd();
        bool SockIsOpen();
        void SockSend(const std::string& msg);
        int SockRecvPort();
        std::string SockRecvAdr();

        Address m_Source;
        int m_HBeatInterval;
        std::string m_AppName;
        std::string m_AppVersion;
        std::set<std::string> m_Groups;
        std::set<std::string> m_Filters;

        std::vector<std::string> m_PreSend;
        SchemaObject *m_HBeatMsg;
        HeartBeatType m_HBeatType;
        time_t m_LastHBeat=0;

        bool m_bAnswerAllMsg;
        std::vector<IExtension*> m_ExtensionClass;

        #ifndef XPLLIB_NOLOG
          std::ofstream m_logStream;
          std::string m_logFile;
          SimpleLog* m_Log;
          SimpleLog m_SimpleLog;
          SimpleLog::DefaultWriter m_logWriter;
          SimpleLog::DefaultFilter m_logFilter;
        #endif

        #ifndef XPLLIB_NOSOCK
          SimpleSockUDP m_SenderSock;
          SimpleSockUDP m_ReceiverSock;
          std::string m_networkInterface;
          void DiscoverTCPPort();
        #else
          std::string m_SockRecvAdr;
          int m_SockRecvPort;
          ISockSend *m_SockSend;
        #endif

        #ifndef XPLLIB_NOCONF
          std::vector<IExtensionConfig*> m_ExtensionConfigClass;
          std::string GetConfigFileName();
          bool m_bLoadConfig;
          std::string m_ConfigFile;
        #endif
};

#ifdef XPLLIB_NOSOCK
class xPLDevice::ISockSend
{
    public:
        virtual bool IsOpen() = 0;
        virtual void Send(std::string const& xplmsg) = 0;
};
#endif

class xPLDevice::IExtension
{
    public:
        virtual bool MsgAnswer(SchemaObject& msg) = 0;
};

#ifndef XPLLIB_NOCONF
class xPLDevice::IExtensionConfig
{
    public:
        virtual bool MsgAnswer(SchemaObject& msg) = 0;
        virtual void LoadConfig(SimpleIni& iniFile) = 0;
        virtual void SaveConfig(SimpleIni& iniFile) = 0;
};
#endif

class xPLDevice::Exception: public std::exception
{
    public:
        Exception(int number, std::string const& message) throw();
        ~Exception() throw();
        const char* what() const throw();
        int GetNumber() const throw();

    private:
        int m_number;
        std::string m_message;
};

}
#endif // XPL_DEVICE_H
