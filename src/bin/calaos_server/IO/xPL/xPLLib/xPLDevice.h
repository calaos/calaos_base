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

/*** MAIN PAGE FOR DOXYGEN *************************************************************************/
/// \mainpage xPPDevice Class Documentation
/// \section intro_sec Introduction
///
/// This class allows you to easily manage network commun,ication, with less than 10 methods.\n
/// To use, include in your project xPPDevice.cpp and xPPDevice.h and ... (ToDo).
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
/// (Compilation directives define LINUX or WIN only necessary for colours in unit tests)
///
/// \section example_sec Example
/// \code
/// #include <iostream>
/// #include "xPPDevice.h"
///
/// using namespace std;
///
/// int main()
/// {
///     xPPDevice ini;
///
///     ini.Load("examples\\example1.ini");
///     cout << "SGBD Host : " << ini.GetValue<string>("SGBD", "Host", "127.0.0.1") << endl;
///     cout << "Port TCP : " << ini.GetValue<int>("SGBD", "PortTCP", 3306) << endl;
///     cout << "Database : " << ini.GetValue<string>("SGBD", "BDD", "MyDB") << endl;
///
///     return 0;
/// }
/// \endcode
///
/// \section licence_sec Licence
///  xPPLib is free software : you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.\n\n
///  xPPLib is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.\n\n
///  You should have received a copy of the GNU General Public License along with xPPLib. If not, see <http://www.gnu.org/licenses/>.
///
/***************************************************************************************************/

#ifndef XPL_DEVICE_H
#define XPL_DEVICE_H

#include <time.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include "SimpleSock/SimpleSock.h"
#include "SimpleLog/SimpleLog.h"
#include "SimpleIni/SimpleIni.h"
#include "xPLLib/Address.h"
#include "xPLLib/Schemas/SchemaObject.h"
#include "xPLLib/Schemas/SchemaHbeat.h"


#define LOG_ENTER LOG_DEBUG(m_Log) << "*** Enter ***"
#define LOG_EXIT_OK LOG_DEBUG(m_Log) << "*** Exit OK ***"
#define LOG_EXIT_KO LOG_DEBUG(m_Log) << "*** Exit KO ***"

namespace xPL
{

/// \brief    Very simple class to manage configuration files
/// \details  Class allows you to easily manage configuration files with less than 10 methods.
class xPLDevice
{
    public:
        class Exception;
        class IExtension;
        class IExtensionConfig;
        enum HeartBeatType {HeartBeatBASIC, HeartBeatAPP, ConfigBASIC, ConfigAPP};

        /// \brief    Constructor of xPPDevice
        /// \param    filename         Name of the configuration file.
        /// \details  Constructor of xPPDevice, optionally can load configuration file \a filename, by Load method. If the Load method fails, an exception is raised.
        xPLDevice();
        xPLDevice(const std::string& vendor, const std::string& device);
        xPLDevice(const std::string& vendor, const std::string& device, const std::string& instance);
		void Initialisation(const std::string& vendor, const std::string& device, const std::string& instance);
        void AddExtension(IExtension* extensionClass);
        void AddExtension(IExtensionConfig* extensionClass);
        void SetAnswerAllMsg(bool bAllMsg);

        /// \brief    Destructor of xPPDevice
        /// \details  Destructor of xPPDevice, deallocate memory, like Free method.
        ~xPLDevice();

        void SetInstance(const std::string& instance);
        void SetAppName(const std::string& appName, const std::string& appVersion);
        void SetNetworkInterface(const std::string& networkInterface);
        void SetGroups(const std::vector<std::string>& group);
        void SetFilters(const std::vector<std::string>& filter);
        void SetLogLevel(int level);
        void SetLogModule(const std::string& module);
        void SetLogFunction(const std::string& funct);
        void SetLogDestination(const std::string& destination);
        void SetHeartBeatInterval(int interval);
        void SetHeartBeatType(HeartBeatType type);
        unsigned short GetTCPPort();
        bool MsgForMe(SchemaObject& msg);

        std::string GetInstance();
        SimpleLog* GetLogHandle();
        HeartBeatType GetHeartBeatType();

        void Open();
        void Close();
        bool WaitRecv(int delay);
        void SendMessage(ISchema *Schema, const std::string& target);

		bool isDevice(const std::string& deviceName);

        std::string GetConfigFolder();
        bool LoadConfig();
        bool SaveConfig();
        void SetConfigFileName(const char* fileName);

    private:
        std::string GetConfigFileName();
        void DiscoverTCPPort();
        bool FilterAllow(const SchemaObject& msg);
        bool InGroup(const std::string& target);
        bool MsgAnswer(SchemaObject& msg);
        void SetHeartBeat(HeartBeatType type, int interval);
        void SendHeartBeat();
        void SendHeartBeatEnd();

        Address m_Source;
        int m_HBeatInterval;
        std::string m_AppName;
        std::string m_AppVersion;
        std::vector<std::string> m_Groups;
        std::vector<std::string> m_Filters;

        std::vector<std::string> m_PreSend;
        SchemaObject *m_HBeatMsg;
        HeartBeatType m_HBeatType;
        bool m_bAnswerAllMsg;
        bool m_bLoadConfig;
        std::vector<IExtension*> m_ExtensionClass;
        std::vector<IExtensionConfig*> m_ExtensionConfigClass;

        std::ofstream m_logStream;
        std::string m_logFile;
        SimpleLog* m_Log;
        SimpleLog m_SimpleLog;
        SimpleLog::DefaultWriter m_logWriter;
        SimpleLog::DefaultFilter m_logFilter;

        SimpleSockUDP m_SenderSock;
        SimpleSockUDP m_ReceiverSock;
        std::string m_networkInterface;

        std::string m_ConfigFile;
};

class xPLDevice::IExtension
{
    public:
        virtual bool MsgAnswer(SchemaObject& msg) = 0;
};

class xPLDevice::IExtensionConfig
{
    public:
        virtual bool MsgAnswer(SchemaObject& msg) = 0;
        virtual void LoadConfig(SimpleIni& iniFile) = 0;
        virtual void SaveConfig(SimpleIni& iniFile) = 0;
};

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
