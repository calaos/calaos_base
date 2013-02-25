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
#include <AudioManager.h>

using namespace Calaos;

AudioManager::AudioManager()
{
}

AudioManager::~AudioManager()
{
        for (int i = 0;i < players.size();i++)
                delete players[i];

        players.clear();
}

AudioManager &AudioManager::Instance()
{
        static AudioManager inst;

        return inst;
}

void AudioManager::Add(AudioPlayer *player, std::string dbhost)
{
        player->set_param("pid", Utils::to_string(players.size()));
        players.push_back(player);
}

void AudioManager::Delete(int pos)
{
        vector<AudioPlayer *>::iterator iter = players.begin();
        for (int i = 0;i < pos;iter++, i++) ;
        delete players[pos];
        players.erase(iter);
}

int AudioManager::searchIdOf(Output *output)
{
        vector<AudioPlayer *>::iterator it;
        int i = 0;
        for(it = players.begin();it != players.end();it++)
        {
                if((*it)->get_output() == output)
                        return i;
                i++;
        }
        return -1;
}
