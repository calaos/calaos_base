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
#ifndef S_IPCam_H
#define S_IPCam_H

#include <Calaos.h>
#include <Output.h>
#include <Input.h>
#include <CamInput.h>
#include <CamOutput.h>
#include <tcpsocket.h>
#include <Params.h>
#include <vector>

namespace Calaos
{

class IPCam
{
        protected:
                Params param;
                Output *aoutput;
                Input *ainput;
                Params caps;

        public:
                IPCam(Params &p);
                virtual ~IPCam();

                //Standard IPCam functions.
                virtual std::string get_cam_url() { return "http://" + param["host"] + ":" + param["port"]; } //return the url of the cam webpage
                virtual std::string get_mpeg_stream() { return ""; } //return the mpeg url stream
                virtual std::string get_mjpeg_stream() { return ""; } //return the mjpeg url stream
                virtual std::string get_picture() { return ""; } //return the url for a single frame

                //this is used for internal purpose (The CamServer relay)
                virtual std::string get_picture_real() { return get_picture(); } //return the real url for a single frame

                //Capabilities
                /*************************************************
                 List of capabilities:
                  * ptz : bool
                  * position : int (number of memory position. if 0, position is not available)
                  * resolution : string (list of resolution, space separated)
                  * led : bool (to activate leds)
                  * buzzer : bool (to activate buzzer)
                  * privacy: bool (to activate privacy mode)
                  * quality: int (range for quality level)
                  * brightness: int (range)
                  * contrast: int (range)
                  * color: int (range)
                  * saturation: int (range)
                  * sharpness: int (range)
                  * hue: int (range)
                **************************************************/
                virtual Params getCapabilities() { return caps; }
                virtual void activateCapabilities(std::string caps, std::string cmd, std::string value) { }

                std::string get_param(std::string opt) { return param[opt]; }
                void set_param(std::string opt, std::string val) { param.Add(opt, val); }
                Params &get_params() { return param; }
                Output *get_output() { return aoutput; }
                Input *get_input() { return ainput; }

                virtual bool LoadFromXml(TiXmlElement *node)
                        { return false; }
                virtual bool SaveToXml(TiXmlElement *node);
};

}

#endif
