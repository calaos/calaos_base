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
#include "Planet.h"
#include "IOFactory.h"

namespace Calaos {

REGISTER_IO(Planet)

Planet::Planet(Params &p):
    IPCam(p),
    saturation("127"),
    sharpness("127"),
    contrast("127"),
    hue("127"),
    brightness("127")
{
    //TODO: add iodoc

    //Set up capabilities
    if (param["model"] == "ICA-300" || param["model"] == "ICA-302" ||
        param["model"] == "ICA-500")
    {
        caps.Add("resolution", "176x144 352x288 320x240 640x480");
        caps.Add("quality", "5");
        caps.Add("saturation", "256");
        caps.Add("sharpness", "256");
        caps.Add("contrast", "256");
        caps.Add("hue", "256");
        caps.Add("brightness", "256");
    }

    if (param["model"] == "ICA-500")
    {
        caps.Add("ptz", "true");
        caps.Add("position", "16");
    }

    if (param["model"] == "ICA-210" || param["model"] == "ICA-210W")
    {
        caps.Add("resolution", "176x144 320x240 640x480");
        caps.Add("quality", "3");
        caps.Add("ptz", "true");
        caps.Add("position", "8");
        caps.Add("led", "true");
        caps.Add("buzzer", "true");
        caps.Add("privacy", "true");
        caps.Add("brightness", "127");
        caps.Add("color", "127");
        caps.Add("contrast", "127");
        caps.Add("sharpness", "11");
    }
}

std::string Planet::getVideoUrl()
{
    std::string url, user;

    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";

    if (param["model"] == "ICA-300" || param["model"] == "ICA-302" || param["model"] == "ICA-500")
    {
        url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/GetData.cgi";
    }
    else if (param["model"] == "ICA-210" || param["model"] == "ICA-210W")
    {
        //Nothing
    }

    return url;
}

std::string Planet::getPictureUrl()
{
    std::string url, user;

    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";

    if (param["model"] == "ICA-300" || param["model"] == "ICA-302" || param["model"] == "ICA-500")
    {
        url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/Jpeg/CamImg.jpg";
    }
    else
    {
        url = "http://" + user + param["host"] + ":" + param["port"];
        url += "/goform/video2";
    }

    return url;
}

void Planet::activateCapabilities(std::string cap, std::string cmd, std::string value)
{
    if (!caps.Exists(cap)) return;

    std::string user;
    if (param.Exists("username"))
        user = param["username"] + ":" + param["password"] + "@";

    if (param["model"] == "ICA-300" || param["model"] == "ICA-302" || param["model"] == "ICA-500")
    {
        if (cap == "resolution" && cmd == "set")
        {
            if (value == "176x144") resolution = "0";
            if (value == "352x288") resolution = "1";
            if (value == "320x240") resolution = "2";
            if (value == "640x480") resolution = "3";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Change_Resolution.cgi?ResType=" + resolution;
            Calaos::CallUrl(url);
        }
        else if (cap == "quality" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["quality"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                quality = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Change_Compress_Ratio.cgi?Ratio=" + quality;
            Calaos::CallUrl(url);
        }
        else if (cap == "saturation" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["saturation"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                saturation = value;

            std::string _size;
            if (resolution == "0") _size = "176";
            if (resolution == "1") _size = "352";
            if (resolution == "2") _size = "320";
            if (resolution == "3") _size = "640";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Set_Camera.cgi?Saturation=" + saturation;
            url += "&Sharpness=" + sharpness;
            url += "&Contrast=" + contrast;
            url += "&Hue=" + hue;
            url += "&Image_Size=" + _size;
            url += "&Image_Quality=" + quality;
            url += "&CamPan=0&CamTilt=0&Exposure_Mode_AM=0&Display_Mode=0&Camera_Tour=0&Camera_Reset=0";
            Calaos::CallUrl(url);
        }
        else if (cap == "sharpness" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["sharpness"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                sharpness = value;

            std::string _size;
            if (resolution == "0") _size = "176";
            if (resolution == "1") _size = "352";
            if (resolution == "2") _size = "320";
            if (resolution == "3") _size = "640";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Set_Camera.cgi?Saturation=" + saturation;
            url += "&Sharpness=" + sharpness;
            url += "&Contrast=" + contrast;
            url += "&Hue=" + hue;
            url += "&Image_Size=" + _size;
            url += "&Image_Quality=" + quality;
            url += "&CamPan=0&CamTilt=0&Exposure_Mode_AM=0&Display_Mode=0&Camera_Tour=0&Camera_Reset=0";
            Calaos::CallUrl(url);
        }
        else if (cap == "contrast" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["contrast"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                contrast = value;

            std::string _size;
            if (resolution == "0") _size = "176";
            if (resolution == "1") _size = "352";
            if (resolution == "2") _size = "320";
            if (resolution == "3") _size = "640";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Set_Camera.cgi?Saturation=" + saturation;
            url += "&Sharpness=" + sharpness;
            url += "&Contrast=" + contrast;
            url += "&Hue=" + hue;
            url += "&Image_Size=" + _size;
            url += "&Image_Quality=" + quality;
            url += "&CamPan=0&CamTilt=0&Exposure_Mode_AM=0&Display_Mode=0&Camera_Tour=0&Camera_Reset=0";
            Calaos::CallUrl(url);
        }
        else if (cap == "hue" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["hue"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                hue = value;

            std::string _size;
            if (resolution == "0") _size = "176";
            if (resolution == "1") _size = "352";
            if (resolution == "2") _size = "320";
            if (resolution == "3") _size = "640";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Set_Camera.cgi?Saturation=" + saturation;
            url += "&Sharpness=" + sharpness;
            url += "&Contrast=" + contrast;
            url += "&Hue=" + hue;
            url += "&Image_Size=" + _size;
            url += "&Image_Quality=" + quality;
            url += "&CamPan=0&CamTilt=0&Exposure_Mode_AM=0&Display_Mode=0&Camera_Tour=0&Camera_Reset=0";
            Calaos::CallUrl(url);
        }
        else if (cap == "brightness" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["brightness"], _q);
            Utils::from_string(value, q);

            if (q >= 0 && q < _q)
                brightness = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/Change_Brightness.cgi?Brightness=" + brightness;
            Calaos::CallUrl(url);
        }
    }

    if (param["model"] == "ICA-500")
    {
        if (cap == "ptz" && cmd == "move")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/MoveCamera.cgi?Dir=";

            if (value == "home") url += "Home";
            if (value == "left") url += "Left";
            if (value == "right") url += "Right";
            if (value == "up") url += "Up";
            if (value == "down") url += "Down";

            Calaos::CallUrl(url);
        }
        else if (cap == "position" && cmd == "recall")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/MoveCamera.cgi?Dir=Recall&CameraParam=" + value;

            Calaos::CallUrl(url);
        }
        else if (cap == "position" && cmd == "save")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/MoveCamera.cgi?Dir=Preset&CameraParam=" + value;

            Calaos::CallUrl(url);
        }
    }

    if (param["model"] == "ICA-210" || param["model"] == "ICA-210W")
    {
        if (cap == "resolution" && cmd == "set")
        {
            if (value == "176x144") resolution = "0";
            if (value == "320x240") resolution = "1";
            if (value == "640x480") resolution = "2";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/resSel?f_ResIdxSel=" + resolution;
            Calaos::CallUrl(url);
        }
        else if (cap == "quality" && cmd == "set")
        {
            if (value == "0") quality = "4";
            if (value == "1") quality = "2";
            if (value == "2") quality = "0";

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/qualSel?f_quality=" + quality;
            Calaos::CallUrl(url);
        }
        if (cap == "ptz" && cmd == "move")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/ptact?f_mvaction=";

            if (value == "home") url += "2";
            if (value == "left") url += "-3";
            if (value == "right") url += "-4";
            if (value == "up") url += "-1";
            if (value == "down") url += "-2";

            Calaos::CallUrl(url);
        }
        else if (cap == "position" && cmd == "recall")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/ptpreset?f_presetact=" + value;

            Calaos::CallUrl(url);
        }
        else if (cap == "position" && cmd == "save")
        {
            int pos;
            Utils::from_string(value, pos);
            pos--;
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/adminptpreset?f_presetidx=" + Utils::to_string(pos);

            Calaos::CallUrl(url);
        }
        else if (cap == "led")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/devact?f_devno=2&f_devact=1";

            Calaos::CallUrl(url);
        }
        else if (cap == "buzzer")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/devact?f_devno=1&f_devact=1";

            Calaos::CallUrl(url);
        }
        else if (cap == "privacy" && cmd == "set" && value == "true")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/ptact?f_mvaction=1";

            Calaos::CallUrl(url);
        }
        else if (cap == "privacy" && cmd == "set" && value == "false")
        {
            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/privacymode_unset";

            Calaos::CallUrl(url);
        }
        else if (cap == "brightness" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["brightness"], _q);
            Utils::from_string(value, q);

            if (q > 0 && q <= _q)
                brightness = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/cameraconfig?brightness=" + brightness;
            url += "&contrast=" + contrast;
            url += "&color=" + color;
            url += "&sharp=" + sharpness;
            url += "&quality=" + quality;
            url += "&f_resolution=" + resolution;
            Calaos::CallUrl(url);
        }
        else if (cap == "color" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["color"], _q);
            Utils::from_string(value, q);

            if (q > 0 && q <= _q)
                color = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/cameraconfig?brightness=" + brightness;
            url += "&contrast=" + contrast;
            url += "&color=" + color;
            url += "&sharp=" + sharpness;
            url += "&quality=" + quality;
            url += "&f_resolution=" + resolution;
            Calaos::CallUrl(url);
        }
        else if (cap == "contrast" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["contrast"], _q);
            Utils::from_string(value, q);

            if (q > 0 && q <= _q)
                contrast = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/cameraconfig?brightness=" + brightness;
            url += "&contrast=" + contrast;
            url += "&color=" + color;
            url += "&sharp=" + sharpness;
            url += "&quality=" + quality;
            url += "&f_resolution=" + resolution;
            Calaos::CallUrl(url);
        }
        else if (cap == "sharpness" && cmd == "set")
        {
            int _q, q;
            Utils::from_string(caps["sharpness"], _q);
            Utils::from_string(value, q);

            if (q > 0 && q <= _q)
                sharpness = value;

            std::string url = "http://" + user + param["host"] + ":" + param["port"];
            url += "/goform/cameraconfig?brightness=" + brightness;
            url += "&contrast=" + contrast;
            url += "&color=" + color;
            url += "&sharp=" + sharpness;
            url += "&quality=" + quality;
            url += "&f_resolution=" + resolution;
            Calaos::CallUrl(url);
        }
    }
}

//Move ICA-210 click on screen
//http://10.0.0.23/goform/ptmovepos?dx=-314&dy=90&res=2
//where dx and dy are relative to the center of the current frame

}
