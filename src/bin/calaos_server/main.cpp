/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
 **
 **  This file is part of Calaos.
 **
 **  Calaos is free software; you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation; either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Calaos is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Foobar; if not, write to the Free Software
 **  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 **
 ******************************************************************************/
#include <Ecore.h>
#include <EcoreTimer.h>
#include "Calaos.h"
#include "Room.h"
#include "ListeRoom.h"
#include "IOFactory.h"
#include "CalaosConfig.h"
#include "ListeRule.h"
#include "UDPServer.h"
#include "NTPClock.h"
#include "WagoMap.h"
#include "HttpServer.h"
#include "Zibase.h"
#include "Prefix.h"
#include "Audio/AVRManager.h"

namespace Calaos {

// Globals
static UDPServer *udpserver = NULL;
static UDPServer *wserver = NULL;
static EcoreTimer *watchdogLoop = NULL;

static void echoVersion(char **argv)
{
    std::cout << "Calaos Version: \n\t" PACKAGE_STRING << std::endl;
}

static void echoUsage(char **argv)
{
    echoVersion(argv);
    std::cout << _("Usage:\n\t") << argv[0] << _(" [options]") << std::endl;
    std::cout << std::endl << _("\tOptions:\n");
    std::cout << _("\t-h, --help\tDisplay this help.\n");
    std::cout << _("\t--config <path>\tSet <path> as the directory for config files.\n");
    std::cout << _("\t--cache <path>\tSet <path> as the directory for cache files.\n");
    std::cout << _("\t--gendoc <path>\tGenerate Io documentation in <path>\n");
    std::cout << _("\t-v, --version\tDisplay current version and exit.\n");
    std::cout << std::endl;
}

int main (int argc, char **argv)
{
    InitEinaLog("server");
    //init ecore system
    eina_init();
    ecore_init();
    ecore_con_init();

    std::vector<Params> params;
    Params p;
    p.Add("test", "valeur");
    params.push_back(p);

    for (auto p2 : params)
    {
        std::cout << p2.size() << std::endl;
    }


    std::cout << "Calaos Server Daemon - http://www.calaos.fr" << std::endl;

    Prefix::Instance(argc, argv);

    //Check command line args
    if (argvOptionCheck(argv, argv + argc, "-h") ||
        argvOptionCheck(argv, argv + argc, "--help"))
    {
        echoUsage(argv);
        exit(0);
    }

    //Check command line args
    if (argvOptionCheck(argv, argv + argc, "-v") ||
        argvOptionCheck(argv, argv + argc, "--version"))
    {
        echoVersion(argv);
        exit(0);
    }

    char *gendocDir = argvOptionParam(argv, argv + argc, "--gendoc");
    if (gendocDir)
    {
        IOFactory::Instance().genDoc(gendocDir);
        exit(0);
    }

    char *confdir = argvOptionParam(argv, argv + argc, "--config");
    char *cachedir = argvOptionParam(argv, argv + argc, "--cache");

    Utils::initConfigOptions(confdir, cachedir);

    srand(time(NULL));

    //Ensure calling order of destructors
    AVRManager::Instance();
    EventManager::Instance();
    ListeRule::Instance();
    ListeRoom::Instance();

    ecore_app_args_set(argc, (const char **)argv);

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

    //Start Json API server
    unsigned short port = JSONAPI_PORT;
    std::string tmp =  Utils::get_config_option("port_api");
    if (!tmp.empty())
        from_string(tmp, port);
    HttpServer::Instance(port);

    NTPClock::Instance();

    if (enable_udp)
    {
        //Start UDP server for wago events
        wserver = new UDPServer(WAGO_LISTEN_PORT);
    }

    cInfo() <<  "### All services started successfully, entering main loop ###";

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

    HttpServer::Instance().disconnectAll();

    //Stop all wagomaps and wait for their threads to terminate correctly.
    WagoMap::stopAllWagoMaps();

    //Stop all zibase
    Zibase::stopAllZibase();

    //Clean up evrything
    delete eventLoop;
    if (watchdogLoop)
    {
        delete watchdogLoop;
        watchdogLoop = NULL;
    }
    delete wserver;
    delete udpserver;

    Utils::FreeEinaLogs();

    return 0;
}

}

int main (int argc, char **argv)
{
  return Calaos::main(argc, argv);
}
