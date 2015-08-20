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
#ifndef S_AUDIOPLAYERDATA_H
#define S_AUDIOPLAYERDATA_H

#include "Calaos.h"

namespace Calaos
{

class AudioPlayerData;

typedef sigc::slot<void, AudioPlayerData> AudioRequest_cb;
typedef sigc::signal<void, AudioPlayerData> AudioRequest_signal;

class AudioPlayerData
{
private:
    AudioPlayerData *chain_data; //used to chain multiple calls

public:
    AudioPlayerData():
        chain_data(NULL)
    {}
    AudioPlayerData(const AudioPlayerData &data)
    {
        params = data.params;
        vparams = data.vparams;
        ivalue = data.ivalue;
        ivalue2 = data.ivalue2;
        svalue = data.svalue;
        dvalue = data.dvalue;
        callback = data.callback;
        user_data = data.user_data;

        if (data.chain_data)
            chain_data = new AudioPlayerData(*data.chain_data);
        else
            chain_data = NULL;
    }
    AudioPlayerData &operator=(const AudioPlayerData &data)
    {
        params = data.params;
        vparams = data.vparams;
        ivalue = data.ivalue;
        ivalue2 = data.ivalue2;
        svalue = data.svalue;
        dvalue = data.dvalue;
        callback = data.callback;
        user_data = data.user_data;

        if (data.chain_data)
            chain_data = new AudioPlayerData(*data.chain_data);
        else
            chain_data = NULL;

        return *this;
    }

    ~AudioPlayerData()
    {
        if (chain_data)
            delete chain_data;
    }

    Params params;
    vector<Params> vparams;
    int ivalue, ivalue2;
    string svalue;
    double dvalue;

    void set_chain_data(AudioPlayerData *data)
    {
        chain_data = data;
    }

    AudioPlayerData &get_chain_data()
    {
        if (!chain_data)
            set_chain_data(new AudioPlayerData);

        return *chain_data;
    }

    AudioRequest_cb callback;
    void *user_data; //user data
};

}

#endif
