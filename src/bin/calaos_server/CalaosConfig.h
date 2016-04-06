/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#include "EcoreTimer.h"
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

    void initEetDescriptors();
    void releaseEetDescriptors();
    void loadStateCache();
    void saveStateCache();

    Eina_Hash *cache_states;
    EcoreTimer *saveCacheTimer;

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

    void SaveValueIO(std::string id, std::string value, bool save = true);
    bool ReadValueIO(std::string id, std::string &value);
};

}
#endif
