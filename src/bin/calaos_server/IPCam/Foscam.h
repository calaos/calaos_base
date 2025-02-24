/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef S_FOSCAM_H
#define S_FOSCAM_H

#include "IPCam.h"
#include "Timer.h"

namespace Calaos
{

class Foscam: public IPCam
{
protected:
    string resolution, quality, camera;

public:
    Foscam(Params &p);

    //Standard IPCam functions.
    std::string getVideoUrl(); //return the mpeg url stream
    std::string getPictureUrl(); //return the url for a single frame

    virtual void activateCapabilities(std::string capability, std::string cmd, std::string value);
};

}

#endif
