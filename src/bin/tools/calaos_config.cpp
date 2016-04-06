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

//
// Write/Read config options from local_config.xml
//

#include <Utils.h>
#include <Ecore_File.h>

void print_usage(void)
{
    std::cout << "Calaos Configuration Utility." << std::endl;
    std::cout << "(c)2013 Calaos Team" << std::endl << std::endl;
    std::cout << "Usage:\tcalaos_config <action> [params]" << std::endl << std::endl;
    std::cout << "Where action can be:" << std::endl;
    std::cout << "\tlist\t\tLists all keys:values" << std::endl;
    std::cout << "\tset [key]\tSet the value for the specified key" << std::endl;
    std::cout << "\tget [key]\tPrint the value for the specified key" << std::endl;
    std::cout << "\tdel [key]\tDelete entry for the specified key" << std::endl;
}

int main (int argc, char **argv)
{
    if (argc < 2)
    {
        print_usage();
        return 1;
    }
    std::string action = argv[1];

    Utils::InitEinaLog("config");

    Utils::initConfigOptions(nullptr, nullptr, true);

    if (action == "get")
    {
        std::string key = argv[2];

        if (key == "hwid")
            std::cout << Utils::getHardwareID();
        else
            std::cout << Utils::get_config_option(key);
    }
    else if (action == "set")
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }
        std::string key = argv[2];
        std::string value = argv[3];

        if (key == "hwid")
            cError() <<  "Can't change hwid";
        else
            Utils::set_config_option(key, value);
    }
    else if (action == "list")
    {
        Params options;
        Utils::get_config_options(options);
        std::cout << "Local configuration:" << std::endl;
        for (int i = 0;i < options.size();i++)
        {
            std::string key, value;
            options.get_item(i, key, value);
            std::cout << key << ": " << value << std::endl;
        }
    }
    else if (action == "del")
    {
        Utils::del_config_option(argv[2]);
    }
    else
    {
        print_usage();
        return 1;
    }

    system("sync");

    Utils::FreeEinaLogs();

    return 0;
}

