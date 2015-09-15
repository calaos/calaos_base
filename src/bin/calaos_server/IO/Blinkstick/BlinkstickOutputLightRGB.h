/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#ifndef BlinkstickOutputLightRGB_H
#define BlinkstickOutputLightRGB_H

#include <libusb-1.0/libusb.h>

#include "EcoreTimer.h"
#include "OutputLightRGB.h"

namespace Calaos
{

class BlinkstickOutputLightRGB : public OutputLightRGB
{
private:
    string m_serial;
    int m_nbLeds;
    bool blinkstickSerialGet(libusb_device *device, unsigned char *serial, libusb_device_handle **handle);

    struct libusb_device_handle *m_device_handle = nullptr;
protected:
    virtual void setColorReal(const ColorValue &c, bool s);

public:
    BlinkstickOutputLightRGB(Params &p);
    ~BlinkstickOutputLightRGB();
};

}

#endif // BlinkstickOutputLightRGB_H
