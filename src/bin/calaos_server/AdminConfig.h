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

/* config_list, config_set, config_backup and config_restore
 * (docs/08_http_api.md, Configuration). The table is a usability rail, not a security boundary:
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
    //Null, with nothing written, when the backup failed, and when local_config.xml
    //could not be written.
    static Json set(const Json &params);

    static Json backup(std::time_t createdAt);

    //Empty when config_restore may use params, the invalid_request message otherwise
    static std::string restoreRefusal(const Json &params);

    //Empty when a config put may write content as the file name, the reason otherwise: the rules of
    //config_restore. A name that is not one of the three files is not judged here.
    static std::string putFileRefusal(const std::string &name, const std::string &content);

    //What restore() would do with params, once restoreRefusal() accepted them. Writes nothing.
    static Json restoreCheck(const Json &params);

    struct RestoreOutcome
    {
        Json result;             //null on failure
        std::string previous;    //the folder of backups/ holding the state before, if any
        bool restarting = false; //a file was replaced: the server exits, even on failure
        std::string error;       //the message of internal, empty on success
    };

    //Replaces the config files once restoreRefusal() accepted params, after
    //Config::BackupFiles(), and leaves the configuration frozen when one was replaced.
    static RestoreOutcome restore(const Json &params);

    //calaos-backup-<host>-<YYYYMMDD-HHMM>.json, host cut to [A-Za-z0-9._-]{0,32}
    static std::string backupFilename(const std::string &hostname, std::time_t createdAt);

    static std::string hostname();
};

#endif
