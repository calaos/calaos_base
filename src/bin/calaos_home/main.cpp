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
        Utils::initConfigOptions();

        if (!Utils::fileExists(std::string(DEFAULT_CONFIG_PATH) + "calaos_gui_log.conf"))
        {
                //create a default config if it does not exist
                std::ofstream conf(std::string(DEFAULT_CONFIG_PATH"calaos_gui_log.conf").c_str(), std::ofstream::out);
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

        Utils::InitLoggingSystem(string(DEFAULT_CONFIG_PATH) + "calaos_gui_log.conf");

        try
        {
                ApplicationMain::Instance(argc, argv).Run(); //Start main app instance
        }
        catch(exception const& e)
        {
                Utils::logger("root") << Priority::CRIT << "An exception occured: " << e.what() << log4cpp::eol;
        }
        catch(...)
        {
                Utils::logger("root") << Priority::CRIT << "An unknown exception occured !" << log4cpp::eol;
        }

        return 0;
}
