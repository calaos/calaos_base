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
#include "StandardMjpeg.h"
#include "IOFactory.h"

namespace Calaos {

REGISTER_IO(StandardMjpeg)
REGISTER_IO_USERTYPE(standard_mjpeg, StandardMjpeg)

StandardMjpeg::StandardMjpeg(Params &p):
    IPCam(p)
{
    //TODO: add iodoc

    //Nothing
    for (int i = 0;i < p.size();i++)
    {
        std::string a, b;
        p.get_item(i, a, b);
        cDebug() << a << ":" << b;
    }

    //actually this is only for testing UI, it does nothing
    if (param.Exists("ptz"))
    {
        caps.Add("ptz", "true");
        caps.Add("position", "8");
    }
    if (param.Exists("zoom"))
    {
        caps.Add("zoom", "true");
    }
}

StandardMjpeg::~StandardMjpeg()
{
}

std::string StandardMjpeg::getPictureUrl()
{
    std::string url, user;

    if (param["url_jpeg"] != "") url = param["url_jpeg"];

    return url;
}

std::string StandardMjpeg::getVideoUrl()
{
    std::string url;
    if (param["url_mjpeg"] != "")
        url = param["url_mjpeg"];

    return url;
}

}
