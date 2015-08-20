/******************************************************************************
 **  Copyright (c) 2007-2014, Calaos. All Rights Reserved.
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
#ifndef S_IPCam_H
#define S_IPCam_H

#include "IOBase.h"

namespace Calaos
{

class IPCam: public IOBase
{
protected:
    Params caps;

public:
    IPCam(Params &p);
    virtual ~IPCam();

    //Standard IPCam functions.
    virtual std::string getVideoUrl() { return ""; } //return the mjpeg url stream
    virtual std::string getPictureUrl() { return ""; } //return the url for a single frame

    //Capabilities
    /*************************************************
     * List of capabilities:
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
    virtual void activateCapabilities(std::string capability, std::string cmd, std::string value) { }

    virtual DATA_TYPE get_type() { return TSTRING; }

    virtual bool set_value(std::string val);

    virtual bool SaveToXml(TiXmlElement *node);
};

}

#endif
