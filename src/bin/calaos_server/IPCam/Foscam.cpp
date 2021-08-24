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
#include "Foscam.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(Foscam)

Foscam::Foscam(Params &p):
    IPCam(p)
{
    ioDoc->descriptionBaseSet(_("Foscam IP Camera/Encoder. Camera can be viewed directly inside calaos and used in rules."));
    ioDoc->paramAdd("ptz", _("Set to true if camera has PTZ support"), IODoc::TYPE_BOOL, false, "false");
    
    if (param["ptz"] == "1" || param["ptz"] == "true")
        caps.Add("ptz", "true");    
}

std::string Foscam::getVideoUrl()
{
    std::string url;
    url = "http://" + param["host"] + ":" + param["port"];
    url += "/cgi-bin/CGIStream.cgi";
    url += "?cmd=GetMJStream";
    url += "&usr=" + param["username"] + "&pwd=" + param["password"];

    return url;
}

std::string Foscam::getPictureUrl()
{
    std::string url;
    url = "http://" + param["host"] + ":" + param["port"];
    url += "/cgi-bin/CGIProxy.fcgi";
    url += "?cmd=snapPicture2";
    url += "&usr=" + param["username"] + "&pwd=" + param["password"];

    return url;
}

void Foscam::activateCapabilities(std::string cap, std::string cmd, std::string value)
{
    if (!caps.Exists(cap)) return;
    
    if (cap == "ptz" && cmd == "move")
    {
        string url;
        string valcmd = "";
        if (value == "up") 
            valcmd = "ptzMoveUp";
        else if  (value == "down") 
            valcmd = "ptzMoveDown";
        else if  (value == "right") 
            valcmd = "ptzMoveRight";
        else if  (value == "left") 
            valcmd = "ptzMoveLeft";
        else if  (value == "stop") 
            valcmd = "ptzStopRun";
        else if  (value == "reset") 
            valcmd = "ptzReset";
        else if  (value == "zoomin") 
            valcmd = "zoomIn";
        else if  (value == "zoomout") 
            valcmd = "zoomOut";
        else if  (value == "zoomstop") 
            valcmd = "zoomStop";

        url = "http://" + param["host"] + ":" + param["port"];
        url += "/cgi-bin/CGIProxy.fcgi";
        url += "?cmd=" + valcmd; 
        url += "&usr=" + param["username"] + "&pwd=" + param["password"];

        Calaos::CallUrl(url);
    } 
}
