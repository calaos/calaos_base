/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef S_AdminConfig_H
#define S_AdminConfig_H

#include "Utils.h"

#include <ctime>

/* config_list, config_set and config_backup (docs/08_http_api.md,
 * Configuration). The table is a usability rail, not a security boundary:
 * the HTTP config put of an admin session rewrites the whole local_config.xml.
 * Type, choices, range and description come from the ConfigOptions registry.
 */
class AdminConfig
{
public:
    struct Entry
    {
        const char *key;
        const char *group;
        bool restart; //taken into account after a calaos-server restart
    };

    static const std::vector<Entry> &entries();
    static const Entry *find(const std::string &key);

    static bool isAction(const std::string &action);

    //The result of config_list: {"options": [...]}, never a secret's value
    static Json list();

    //Empty when config_set may write params, the invalid_request message otherwise
    static std::string setRefusal(const Json &params);

    //Writes params once setRefusal() accepted them, after Config::BackupFiles().
    //Null when local_config.xml could not be written.
    static Json set(const Json &params);

    static Json backup(std::time_t createdAt);

    //calaos-backup-<host>-<YYYYMMDD-HHMM>.json, host cut to [A-Za-z0-9._-]{0,32}
    static std::string backupFilename(const std::string &hostname, std::time_t createdAt);

    static std::string hostname();
};

#endif
