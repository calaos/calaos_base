/******************************************************************************
**  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
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
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#ifndef S_CONFIG_H
#define S_CONFIG_H

#include "Calaos.h"
#include <Room.h>
#include <ListeRoom.h>
#include <Input.h>
#include <Output.h>
#include <IOFactory.h>
#include <Condition.h>
#include <Rule.h>
#include <ListeRule.h>
#include <Action.h>
#include <AudioPlayer.h>
#include <AudioManager.h>
#include <IntValue.h>
#include <IPCam.h>
#include <CamManager.h>
#include <Scenario.h>
#include <Calaos.h>
#include <RulesFactory.h>

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
                bool ReadValueIO(string id, string &value);
};

}
#endif
