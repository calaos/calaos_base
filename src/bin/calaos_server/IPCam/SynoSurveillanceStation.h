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
#ifndef S_SYNOSS_H
#define S_SYNOSS_H

#include "IPCam.h"

namespace Calaos
{

class SynoSurveillanceStation: public IPCam
{
public:
    SynoSurveillanceStation(Params &p);
    virtual ~SynoSurveillanceStation();

    virtual void downloadSnapshot(std::function<void(const string &)> dataCb);

private:
    bool isRunning = false;

    std::function<void(const string &)> downloadDataCb = [](const string &){};

    string authUrl;
    string apiSid;
    string snapUrl;

    Json parseJsonResult(const string &data, bool &error);

    void getApiInfo(const string &api, const string &method, const string &version,
                    std::function<void(const string &url)> cb);

    void tryLogin();
    void login(std::function<void(const string &sid)> cb);

    void tryGetSnapshot();
    void getSnapshot(std::function<void(const string &data)> cb);
    void doGetSnapshot();
};

}

#endif
