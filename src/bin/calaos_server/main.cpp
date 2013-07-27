/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <Calaos.h>
#include <Room.h>
#include <ListeRoom.h>
#include <Input.h>
#include <Output.h>
#include <Params.h>
#include <IOFactory.h>
#include <Config.h>
#include <ListeRule.h>
#include <TCPServer.h>
#include <UDPServer.h>
#include <AudioManager.h>
#include <CamServer.h>
#include <Ecore.h>
#include <EcoreTimer.h>
#include <NTPClock.h>
#include <SendMail.h>
#include <WagoMap.h>

#ifdef HAVE_BREAKPAD
#include "client/linux/handler/exception_handler.h"
#endif

using namespace Calaos;

// Globals
static UDPServer *udpserver = NULL;
static UDPServer *wserver = NULL;
static CamServer *camserver = NULL;
static EcoreTimer *watchdogLoop = NULL;

static void echoUsage(char **argv)
{
        cout << "Usage:\n\t" << argv[0] << " [options]" << endl;
        cout << endl << "\tOptions:\n";
        cout << "\t-h, --help\tDisplay this help.\n";
        cout << "\t--config <path>\tSet <path> as the directory for config files.\n";
        cout << "\t--cache <path>\tSet <path> as the directory for cache files.\n";
        cout << endl;
}

#ifdef HAVE_BREAKPAD
static bool dumpCallback(const char* dump_path,
                         const char* minidump_id,
                         void* context,
                         bool succeeded)
{
        printf("Dump path: %s/%s.dmp\n", dump_path, minidump_id);

        string cmd = "/sbin/crash_report.sh calaosd ";
        cmd += minidump_id;

        //Gather system informations and compress it.
        int unused = system(cmd.c_str());
        (void)unused;

        return succeeded;
}
#endif

int main (int argc, char **argv)
{
        cout << "Calaos Server Daemon - http://www.calaos.fr" << endl;

        #ifdef HAVE_BREAKPAD
        google_breakpad::ExceptionHandler eh("/mnt/ext3/backtraces", NULL, dumpCallback, NULL, true);
        #endif

        //Check command line args
        if (argvOptionCheck(argv, argv + argc, "-h") ||
            argvOptionCheck(argv, argv + argc, "--help"))
        {
                echoUsage(argv);
                exit(0);
        }

        char *confdir = argvOptionParam(argv, argv + argc, "--config");
        char *cachedir = argvOptionParam(argv, argv + argc, "--cache");


        Utils::initConfigOptions(confdir, cachedir);

        if (!Utils::fileExists(Utils::getConfigFile("calaosd_log.conf")))
        {
                //create a default config if it does not exist
                std::ofstream conf(Utils::getConfigFile("calaosd_log.conf").c_str(), std::ofstream::out);
                conf << "log4j.rootCategory=INFO, Console" << std::endl;
                conf << "log4j.appender.Console=org.apache.log4j.ConsoleAppender" << std::endl;
                conf << "log4j.appender.Console.layout=org.apache.log4j.PatternLayout" << std::endl;
                conf << "log4j.appender.Console.layout.ConversionPattern=%p %c : %m%n" << std::endl;
                conf << "log4j.appender.Syslog=org.apache.log4j.LocalSyslogAppender" << std::endl;
                conf << "log4j.appender.Syslog.syslogName=calaos_server" << std::endl;
                conf << "log4j.appender.Syslog.facility=0" << std::endl;
                conf << "log4j.appender.Syslog.layout=org.apache.log4j.SimpleLayout" << std::endl;
                conf.close();
        }

        Utils::InitLoggingSystem(Utils::getConfigFile("calaosd_log.conf"));

        srand(time(NULL));

        //init ecore system
        eina_init();
        ecore_init();
        ecore_con_init();

        //Changes the default folder for config files
        char *buf = new char[PATH_MAX];
        char *unused = getcwd(buf, PATH_MAX);
        (void)unused;
        string current_path = buf;
        delete[] buf;
        int unused2 = chdir(ETC_DIR);
        (void)unused2;

        Config::Instance().LoadConfigIO();
        Config::Instance().LoadConfigRule();

        bool enable_udp = true;
        if (argvOptionCheck(argv, argv + argc, "-noudp"))
                enable_udp = false;

        if(enable_udp)
        {
                //Start main UDP communication server
                udpserver = new UDPServer(BCAST_UDP_PORT);
        }

        //Start main TCP server for network commands
        TCPServer::Instance(TCP_LISTEN_PORT);

        NTPClock::Instance();

        //Start IPCam relay
        camserver = new CamServer(5050);
        camserver->Start();

        if (enable_udp)
        {
                //Start UDP server for wago events
                wserver = new UDPServer(WAGO_LISTEN_PORT);
        }

        //Setup mailing system
        SendMail::Instance();

        Utils::logger("root") << Priority::INFO << "### All services started successfully, entering main loop ###" << log4cpp::eol;

        //Check if any Start Rules need to be executed.
        Calaos::StartReadRules::Instance().addIO();
        Calaos::StartReadRules::Instance().ioRead();

        Utils::Watchdog("calaosd");

        //main loop
        EcoreTimer *eventLoop = new EcoreTimer(10. / 1000., (sigc::slot<void>)sigc::mem_fun(ListeRule::Instance(), &ListeRule::RunEventLoop) );
        watchdogLoop = new EcoreTimer(5., (sigc::slot<void>)sigc::bind(sigc::ptr_fun(Utils::Watchdog), "calaosd") );

        //Check config once the main loop is started
        EcoreTimer::singleShot(0.0, sigc::mem_fun(ListeRoom::Instance(), &ListeRoom::checkAutoScenario));

        ecore_main_loop_begin();

        //Stop all wagomaps and wait for their threads to terminate correctly.
        WagoMap::stopAllWagoMaps();

        //Clean up evrything
        delete eventLoop;
        if (watchdogLoop)
        {
                delete watchdogLoop;
                watchdogLoop = NULL;
        }
        delete wserver;
        delete udpserver;

        return 0;
}
