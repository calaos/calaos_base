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
#ifndef S_AUDIOINPUT_H
#define S_AUDIOINPUT_H

#include <Calaos.h>
#include <Input.h>

namespace Calaos
{

class AudioPlayer;

class AudioInput: public Input, public sigc::trackable
{
private:
    AudioPlayer *player;
    std::string answer;
    int status;
    int st;

public:
    AudioInput(Params &p, AudioPlayer *_player);
    ~AudioInput();

    virtual DATA_TYPE get_type() { return TSTRING; }

    virtual void hasChanged();
    virtual std::string get_value_string();

    void set_status(int s) { st = s; hasChanged(); }

    AudioPlayer *get_player() { return player; }
};

}

#endif
