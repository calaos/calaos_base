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
#include "CamOutput.h"
#include "IPCam.h"

using namespace Calaos;

CamOutput::CamOutput(Params &p, IPCam *_cam):
    Output(p),
    cam(_cam)
{
    get_params().Add("gui_type", "camera_output");
    get_params().Add("visible", "false");
    cInfoDom("output") << "CamOutput::CamOutput(): Ok";
}

CamOutput::~CamOutput()
{
    cInfoDom("output") << "CamOutput::~CamOutput(): Ok";
}

bool CamOutput::set_value(std::string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << "CamOutput(" << get_param("id") << "): got action, " << val;

    if (val == "mpeg_stream?")
    {
        answer = cam->get_mpeg_stream();
    }
    else if (val == "mjpeg_stream?")
    {
        answer = cam->get_mjpeg_stream();
    }
    else if (val == "single_frame?")
    {
        answer = cam->get_picture();
    }
    else if (val.compare(0, 5, "move ") == 0)
    {
        val.erase(0, 5);
        cam->activateCapabilities("ptz", "move", val);
    }
    else if (val.compare(0, 5, "save ") == 0)
    {
        val.erase(0, 5);
        cam->activateCapabilities("position", "save", val);
    }
    else if (val.compare(0, 7, "recall ") == 0)
    {
        val.erase(0, 7);
        cam->activateCapabilities("position", "recall", val);
    }

    EventManager::create(CalaosEvent::EventOutputChanged,
                         { { "id", get_param("id") },
                           { "state", val } });

    return true;
}

std::string CamOutput::get_value_string()
{
    return answer;
}
