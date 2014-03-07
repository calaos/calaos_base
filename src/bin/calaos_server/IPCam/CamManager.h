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
#ifndef S_CamManager_H
#define S_CamManager_H

#include <Calaos.h>
#include <IPCam.h>

namespace Calaos
{

class CamManager
{
private:
    CamManager();

    std::vector<IPCam *> ip_cams;
public:
    static CamManager &Instance(); //Singleton
    ~CamManager();

    void Add(IPCam *cam);
    void Delete(int i);
    void Delete(IPCam *obj)
    { ip_cams.erase(std::remove(ip_cams.begin(), ip_cams.end(), obj), ip_cams.end()); }
    int get_size() { return ip_cams.size(); } //return the number of IP Cameras available
    IPCam *get_camera(int i) { return ip_cams[i]; }
};

}

#endif
