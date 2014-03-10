/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <CamManager.h>

using namespace Calaos;

CamManager::CamManager()
{
}

CamManager::~CamManager()
{
    for (uint i = 0;i < ip_cams.size();i++)
        delete ip_cams[i];

    ip_cams.clear();
}

CamManager &CamManager::Instance()
{
    static CamManager inst;

    return inst;
}

void CamManager::Add(IPCam *module)
{
    ip_cams.push_back(module);
}

void CamManager::Delete(int pos)
{
    vector<IPCam *>::iterator iter = ip_cams.begin();
    for (int i = 0;i < pos;iter++, i++) ;
    delete ip_cams[pos];
    ip_cams.erase(iter);
}
