/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
#endif

#include "ApplicationMain.h"

using namespace Utils;

static void echoUsage(char **argv)
{
        cout << "Calaos Home GUI - http://www.calaos.fr" << endl;
        cout << "Usage:\n\t" << argv[0] << " [options]" << endl;
        cout << endl << "\tOptions:\n";
        cout << "\t-h, --help\tDisplay this help.\n";
        cout << "\t--config <path>\tSet <path> as the directory for config files.\n";
        cout << "\t--cache <path>\tSet <path> as the directory for cache files.\n";
        cout << "\t--theme <file.edj>\tUse the given edje file instead of the default.\n";
        cout << "\t--set-elm-config\tForce calaos_home to set the correct elementary config options for touchscreen usage.\n";
        cout << endl;
}

int main(int argc, char **argv)
{

#ifdef ENABLE_NLS
	/* Set the locale defined by the system */
	char *curlocale = setlocale(LC_ALL, "");
	curlocale = curlocale ? 
	  curlocale :
	  setlocale(LC_ALL, "C");
	printf("Current locale : %s\n", curlocale);
	bindtextdomain(PACKAGE, LOCALE_DIR);
	textdomain(PACKAGE);
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

        if (!Utils::fileExists(Utils::getConfigFile("calaos_gui_log.conf")))
        {
                //create a default config if it does not exist
                std::ofstream conf(Utils::getConfigFile("calaos_gui_log.conf").c_str(), std::ofstream::out);
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

        Utils::InitLoggingSystem(getConfigFile("calaos_gui_log.conf"));

        try
        {
                ApplicationMain::Instance(argc, argv).Run(); //Start main app instance
        }
        catch(exception const& e)
        {
                cCritical() <<  "An exception occured: " << e.what();
        }
        catch(...)
        {
                cCritical() <<  "An unknown exception occured !";
        }

        return 0;
}

