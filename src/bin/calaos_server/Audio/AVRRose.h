/******************************************************************************
 **  Copyright (c) 2006-2024, Calaos. All Rights Reserved.
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
#ifndef AVRRose_H
#define AVRRose_H

#include "Calaos.h"
#include "AVReceiver.h"

namespace Calaos
{

//Manage HifiRose devices (like RS520)
class AVRRose: public AVReceiver
{
protected:

    void postRequest(string urlPath, string data, std::function<void(const string &)> dataCb);
    void pollStatus(std::function<void()> nextCb = nullptr);

public:
    AVRRose(Params &p);
    virtual ~AVRRose();

    virtual void Power(bool on, int zone = 1) override;
    virtual void setVolume(int volume, int zone = 1) override;
    virtual void selectInputSource(int source, int zone = 1) override;
    virtual bool hasDisplay() override { return false; }

    virtual void sendCustomCommand(string command) override;
};

}

#endif // AVRRose_H
