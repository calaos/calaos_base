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
#include <StandardMjpeg.h>
#include <CamManager.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_CAMERA(StandardMjpeg)
REGISTER_CAMERA_USERTYPE(standard_mjpeg, StandardMjpeg)

StandardMjpeg::StandardMjpeg(Params &p): IPCam(p)
{
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

std::string StandardMjpeg::get_mpeg_stream()
{
    string url;
    if (param["url_mpeg"] != "") url = param["url_mpeg"];

    return url;
}

std::string StandardMjpeg::get_picture_real()
{
    std::string url, user;

    if (param["url_jpeg"] != "") url = param["url_jpeg"];

    return url;
}

std::string StandardMjpeg::get_mjpeg_stream()
{
    string url;
    if (param["url_mjpeg"] != "")
        url = param["url_mjpeg"];
    else
    {
        //Get id
        int id = -1;
        for (int i = 0;i < CamManager::Instance().get_size();i++)
        {
            if (CamManager::Instance().get_camera(i)->get_param("id") == param["id"])
                id = i;
        }

        //get local ip
        string local_ip = TCPSocket::GetLocalIPFor(param["host"]);

        url = "http://" + local_ip + ":5050/GetCamera.cgi?id=" + Utils::to_string(id);
    }

    return url;
}

std::string StandardMjpeg::get_picture()
{
    string url;
    if (param["url_mjpeg"] != "")
        url = param["url_jpeg"];
    else
    {
        //Get id
        int id = -1;
        for (int i = 0;i < CamManager::Instance().get_size();i++)
        {
            if (CamManager::Instance().get_camera(i)->get_param("id") == param["id"])
                id = i;
        }

        //get local ip
        string local_ip = TCPSocket::GetLocalIPFor(param["host"]);

        url = "http://" + local_ip + ":5050/GetPicture.cgi?id=" + Utils::to_string(id);
    }

    return url;
}

void StandardMjpeg::activateCapabilities(std::string cap, std::string cmd, std::string value)
{
    //Nothing
}
