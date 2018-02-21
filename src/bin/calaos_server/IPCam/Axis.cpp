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
#include "Axis.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(Axis)

Axis::Axis(Params &p):
    IPCam(p),
    quality("30"),
    camera("1")
{
    //TODO: add ioDoc!

    caps.Add("resolution", "176x144 320x240 640x480");
    caps.Add("quality", "100");
    if (param["model"] != "") camera = param["model"];
    if (param["ptz"] == "1" || param["ptz"] == "true")
    {
        caps.Add("ptz", "true");
        caps.Add("position", "8");
    }
    if (param.Exists("zoom_step"))
    {
        caps.Add("zoom", "true");
    }
    if (param.Exists("pan_framesize") && param.Exists("tilt_framesize"))
    {
        caps.Add("moveclick", "true");
    }
    if (param.Exists("resolution"))
    {
        resolution = param["resolution"];
    }
}

std::string Axis::getVideoUrl()
{
    string user;

    if (param["model"] != "") camera = param["model"];

    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";

    std::string url;
    url = "http://" + user + param["host"] + ":" + param["port"];
    url += "/axis-cgi/mjpg/video.cgi";
    url += "?quality=" + quality;
    url += "&camera=" + camera;
    if (!resolution.empty())
        url += "&resolution=" + resolution;

    return url;
}

std::string Axis::getPictureUrl()
{
    string user;

    if (param["model"] != "") camera = param["model"];

    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";


    std::string url;
    url = "http://" + user + param["host"] + ":" + param["port"];
    url += "/axis-cgi/jpg/image.cgi";
    url += "?quality=" + quality;
    url += "&camera=" + camera;
    if (!resolution.empty())
        url += "&resolution=" + resolution;

    return url;
}

void Axis::activateCapabilities(std::string cap, std::string cmd, std::string value)
{
    if (!caps.Exists(cap)) return;

    string user;
    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";

    if (cap == "resolution" && cmd == "set")
    {
        vector<string> res;
        Utils::split(caps["resolution"], res, " ");
        for (uint i = 0;i < res.size();i++)
        {
            if (value == res[i])
                resolution = value;
        }
    }
    else if (cap == "quality" && cmd == "set")
    {
        int _q, q;
        from_string(caps["quality"], _q);
        from_string(value, q);

        if (q >= 0 && q <= _q)
            quality = value;
    }
    else if (cap == "ptz" && cmd == "move")
    {
        string url;

        if (value == "zoomin" || value == "zoomout")
        {
            url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/axis-cgi/com/ptz.cgi?";
            url += "camera=" + camera;
            url += "&rzoom=" + string((value == "zoomout")?"-":"") + param["zoom_step"];
        }
        else
        {
            url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/axis-cgi/com/ptz.cgi?";
            url += "camera=" + camera;
            url += "&move=" + value;
        }

        Calaos::CallUrl(url);
    }
    else if (cap == "moveclick" && cmd == "set")
    {
        //rpan == 68
        //rtilt == 57

        if (!param.Exists("pan_framesize") || !param.Exists("tilt_framesize"))
            return;

        //Parse pan/tilt values
        vector<string> tok;
        split(value, tok, ",");
        if (tok.size() != 2) return;

        double pan, tilt, pan_framesize, tilt_framesize;

        from_string(tok[0], pan);
        from_string(tok[1], tilt);
        from_string(param["pan_framesize"], pan_framesize);
        from_string(param["tilt_framesize"], tilt_framesize);

        //Center coordinates
        pan -= 320.0;
        tilt -= 240.0;

        //Invert coordinates
        pan = 0 - pan;
        tilt = 0 - tilt;

        pan = (pan * pan_framesize) / 320.0;
        tilt = (tilt * tilt_framesize) / 240.0;

        string url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/axis-cgi/com/ptz.cgi?";
        url += "camera=" + camera;
        url += "&rpan=" + Utils::to_string(-pan) + "&rtilt=" + Utils::to_string(tilt);

        Calaos::CallUrl(url);
    }
    else if (cap == "position" && cmd == "recall")
    {
        string url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/axis-cgi/com/ptz.cgi?";
        url += "camera=" + camera;
        url += "&gotoserverpresetno=" + value;

        Calaos::CallUrl(url);
    }
    else if (cap == "position" && cmd == "save")
    {
        string url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/axis-cgi/com/ptz.cgi?";
        url += "camera=" + camera;
        url += "&setserverpresetno=" + value;

        Calaos::CallUrl(url);
    }
}
