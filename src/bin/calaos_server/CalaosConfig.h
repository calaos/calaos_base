/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#ifndef S_CONFIG_H
#define S_CONFIG_H

#include "Calaos.h"
#include "Timer.h"
#include "Room.h"
#include "ListeRoom.h"
#include "IOFactory.h"
#include "ListeRule.h"
#include "RulesFactory.h"

namespace Calaos
{

#define CONFIG_STATES_CACHE_VERSION     1

class Config
{
private:
    Config();

    void loadStateCache();
    void saveStateCache();

    unordered_map<string, string> cache_states;
    unordered_map<string, Params> cache_params;
    std::shared_ptr<Timer> saveCacheTimer;

public:
    static Config &Instance()
    {
        static Config inst;
        return inst;
    }
    ~Config();

    void LoadConfigIO();
    void LoadConfigRule();

    void SaveConfigIO();
    void SaveConfigRule();

    void SaveValueIO(string id, string value, bool save = true);
    void SaveValueParams(string id, Params value, bool save = true);
    bool ReadValueIO(string id, string &value);
    bool ReadValueParams(string id, Params &value);

    void BackupFiles();
};

}
#endif
