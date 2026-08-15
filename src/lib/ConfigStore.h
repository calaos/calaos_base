/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef CALAOS_CONFIGSTORE_H
#define CALAOS_CONFIGSTORE_H

#include <string>
#include <vector>

#include <Params.h>

//-----------------------------------------------------------------------------
namespace Utils
{
void initConfigOptions(char *configdir = NULL, char *cachedir = NULL, bool quiet = false);

std::string getConfigPath();
std::string getCachePath();
std::string getConfigFile(const char *configFile);
std::string getCacheFile(const char *cacheFile);

std::string get_config_option(std::string key, bool no_logger_out = false);
bool set_config_option(std::string key, std::string value);
bool del_config_option(std::string key);
bool get_config_options(Params &options);
//Batched update: every key of toSet is created or updated and every key of
//toDelete is removed, in a single load/modify/atomic write cycle. The file is
//reloaded at call time under the lock, so the keys that are in neither list
//keep the value another process may have given them in the meantime.
bool set_config_options(const Params &toSet, const std::vector<std::string> &toDelete = {});
}

#endif
