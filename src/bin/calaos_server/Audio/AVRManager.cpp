/******************************************************************************
**  Copyright (c) 2006-2012, Calaos. All Rights Reserved.
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
**  along with Calaos; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <AVRManager.h>
#include <AVRPioneer.h>
#include <AVRDenon.h>
#include <AVROnkyo.h>
#include <AVRMarantz.h>
#include <AVRYamaha.h>

using namespace Calaos;

AVRManager::AVRManager()
{
}

AVRManager::~AVRManager()
{
    for (unsigned int i = 0;i < avrs.size();i++)
        delete avrs[i];

    avrs.clear();
}

AVRManager &AVRManager::Instance()
{
    static AVRManager inst;

    return inst;
}

AVReceiver *AVRManager::Create(Params &p)
{
    AVReceiver *receiver = getReceiver(p["host"]);
    if (!receiver)
    {
        if (p["model"] == "pioneer")
            receiver = new AVRPioneer(p);
        else if (p["model"] == "denon")
            receiver = new AVRDenon(p);
        else if (p["model"] == "onkyo")
            receiver = new AVROnkyo(p);
        else if (p["model"] == "marantz")
            receiver = new AVRMarantz(p);
        else if (p["model"] == "yamaha")
            receiver = new AVRYamaha(p);
        else
        {
            cInfoDom("output") << "AVRManager(): Unknown A/V Receiver model " << p["model"];
            return NULL;
        }
        avrs.push_back(receiver);
    }

    receiver->ref_count++;

    return receiver;
}

void AVRManager::Delete(AVReceiver *obj)
{
    bool found = false;
    for (unsigned int i = 0;i < avrs.size() && !found;i++)
    {
        if (avrs[i] == obj)
        {
            obj->ref_count--;
            found = true;
        }
    }

    if (obj->ref_count <= 0)
    {
        avrs.erase(std::remove(avrs.begin(), avrs.end(), obj), avrs.end());
        delete obj;
    }
}

AVReceiver *AVRManager::getReceiver(string host)
{
    for (unsigned int i = 0;i < avrs.size();i++)
    {
        if (avrs[i]->host == host)
            return avrs[i];
    }

    return NULL;
}
