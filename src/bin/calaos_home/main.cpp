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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
#endif

#include "Prefix.h"
#include "ApplicationMain.h"


static void echoVersion(char **argv)
{
    std::cout << "Calaos Version: \n\t" PACKAGE_STRING << std::endl;
}

static void echoUsage(char **argv)
{
    std::cout << "Calaos Home - http://www.calaos.fr" << std::endl;
    echoVersion(argv);
    std::cout << _("Usage:\n\t") << argv[0] << _(" [options]") << std::endl;
    std::cout << std::endl << _("\tOptions:\n");
    std::cout << _("\t-h, --help\tDisplay this help.\n");
    std::cout << _("\t--config <path>\tSet <path> as the directory for config files.\n");
    std::cout << _("\t--cache <path>\tSet <path> as the directory for cache files.\n");
    std::cout << _("\t--theme <file.edj>\tUse the given edje file instead of the default.\n");
    std::cout << _("\t--set-elm-config\tForce calaos_home to set the correct elementary config options for touchscreen usage.\n");
    std::cout << _("\t-v, --version\tDisplay current version and exit.\n");
    std::cout << std::endl;
}

int main(int argc, char **argv)
{
    InitEinaLog("home");

    Prefix::Instance(argc, argv);

#ifdef ENABLE_NLS
    /* Set the locale defined by the system */
    char *curlocale = setlocale(LC_ALL, "");
    curlocale = curlocale ?
                    curlocale :
                    setlocale(LC_ALL, "C");
    cInfo() << "Current locale : " << curlocale;
    bindtextdomain(PACKAGE, Prefix::Instance().localeDirectoryGet().c_str());
    textdomain(PACKAGE);
#endif

    //Check command line args
    if (argvOptionCheck(argv, argv + argc, "-h") ||
        argvOptionCheck(argv, argv + argc, "--help"))
    {
        echoUsage(argv);
        exit(0);
    }

    if (argvOptionCheck(argv, argv + argc, "-v") ||
        argvOptionCheck(argv, argv + argc, "--version"))
    {
        echoVersion(argv);
        exit(0);
    }

    char *confdir = argvOptionParam(argv, argv + argc, "--config");
    char *cachedir = argvOptionParam(argv, argv + argc, "--cache");

    Utils::initConfigOptions(confdir, cachedir);

    try
    {
        ApplicationMain::Instance(argc, argv).Run(); //Start main app instance
    }
    catch(std::exception const& e)
    {
        cCritical() <<  "An exception occured: " << e.what();
    }
    catch(...)
    {
        cCritical() <<  "An unknown exception occured !";
    }
    Utils::FreeEinaLogs();

    return 0;
}

