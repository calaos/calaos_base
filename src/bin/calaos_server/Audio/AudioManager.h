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
#ifndef S_AUDIOMANAGER_H
#define S_AUDIOMANAGER_H

#include <Calaos.h>
#include <AudioPlayer.h>
#include <AudioDB.h>

namespace Calaos
{

class AudioManager
{
private:
    AudioManager();

    std::string host;
    int port;

    vector<AudioPlayer *> players;
public:
    static AudioManager &Instance(); //Singleton
    ~AudioManager();

    void Add(AudioPlayer *player, std::string dbhost = "null");
    void Delete(int i);
    void Delete(AudioPlayer *obj)
    { players.erase(std::remove(players.begin(), players.end(), obj), players.end()); }
    int get_size() { return players.size(); } //return the number of players available
    AudioPlayer *get_player(int i) { return players[i]; }

    int searchIdOf(Output *output);
};

}

#endif
