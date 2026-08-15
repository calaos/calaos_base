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
#include "Gadspot.h"
#include "IOFactory.h"

using namespace Calaos;

//T3.3 decision: Gadspot is a legacy MJPEG driver for vintage hardware
//(hardcoded CGI paths, fixed 640x480 resolution) that is no longer sold or
//maintained. It is KEPT on purpose: unregistering the "Gadspot" type would
//break loading of existing io.xml configs that still reference it (unknown
//type on the installer side, cf. project TODO). Revisit removal only
//together with a config migration path for unknown IO types.
REGISTER_IO(Gadspot)

Gadspot::Gadspot(Params &p):
    IPCam(p)
{
    ioDoc->descriptionBaseSet(_("Gadspot IP Camera. Camera can be viewed directly inside calaos and used in rules."));

    caps.Add("resolution", "640x480");
}

std::string Gadspot::getVideoUrl()
{
    std::string url;
    url = "http://" + param["host"] + ":" + param["port"];
    url += "/GetData.cgi";

    return url;
}

std::string Gadspot::getPictureUrl()
{
    std::string url;
    url = "http://" + param["host"] + ":" + param["port"];
    url += "/Jpeg/CamImg.jpg";

    return url;
}
