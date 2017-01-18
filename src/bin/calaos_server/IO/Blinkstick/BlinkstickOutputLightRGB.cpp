/******************************************************************************
 **  Copyright (c) 2006-2017, Calaos. All Rights Reserved.
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
#include "BlinkstickOutputLightRGB.h"
#include "IOFactory.h"

#define BLINKSTICK_VENDOR_ID 0x20A0
#define BLINKSTICK_PRODUCT_ID 0x41E5

using namespace Calaos;

REGISTER_IO(BlinkstickOutputLightRGB)

BlinkstickOutputLightRGB::BlinkstickOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("BlinkstickOutputLightRGB");
    ioDoc->descriptionSet(_("RGB Light dimmer using a Blinkstick"));
    ioDoc->linkAdd("OLA", _("http://www.blinkstick.com"));
    ioDoc->paramAdd("serial", _("Blinkstick serial to control"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("nb_leds", _("Number of LEDs to control with the blinkstick"), 0, 9999, true, 1);

    if (!param_exists("nb_leds")) set_param("nb_leds", "1");
    m_serial = get_param("serial");
    Utils::from_string(get_param("nb_leds"), m_nbLeds);

    libusb_context *context = NULL;
    libusb_init(&context);
    libusb_device **devices;
    int device_count = libusb_get_device_list(context, &devices);
    cDebugDom("blinkstick") << "Found " << device_count << "devices";
    
    for(int i = 0; i < device_count; i++)
    {
        libusb_device *device = devices[i];
        unsigned char serial[255];
        libusb_device_handle *handle;
        if(blinkstickSerialGet(device, serial, &handle))
        {
            if (m_serial == (char*)serial)
            {
                cInfoDom("blinkstick") << "Found blinkstrick with serial " << serial;
                m_device_handle = handle;
                //unsigned char mode[1];
                //mode[0] = 0; // Mode WS2812
                //libusb_control_transfer(m_device_handle, 0x20, 0x9, 0x2, 0x0004, mode, sizeof(mode), 2);
                break;
            }
            else
            {
                cDebugDom("blinkstrick") << "Serial " << serial << " doesn't match with " << m_serial;
            }
        }
    }
}

BlinkstickOutputLightRGB::~BlinkstickOutputLightRGB()
{
    if (m_device_handle)
        libusb_close(m_device_handle);
}

void BlinkstickOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    unsigned char msg[6];

    memset(msg, 0, sizeof(msg));
    msg[0] = 0x05;
    msg[1] = 0x00;

    if (s)
    {
        msg[3] = (c.getRed());
        msg[4] = (c.getGreen());
        msg[5] = (c.getBlue());
    }

    int ret;
    for (int i = 0; i < m_nbLeds; i++)
    {
        msg[2] = i;
        char retry = 5;
        do
        {
            ret = libusb_control_transfer(m_device_handle, 0x20, 0x9, 0x5, 0x0000, msg, sizeof(msg), 2);
            if (ret >= 0)
            {
                cDebugDom("blinkstick") << "Control Transfer OK";
                break;
            }
            else
                retry--;
        } while(retry >= 0);
        if (retry <= 0)
             cErrorDom("blinkstick") << "Unable to send color";
    }
}

bool BlinkstickOutputLightRGB::blinkstickSerialGet(libusb_device *device, unsigned char *serial, libusb_device_handle **handle)
{
    struct libusb_device_descriptor desc;

    int result = libusb_get_device_descriptor(device, &desc);

    if (result >= 0) 
    {
        if ((desc.idVendor == BLINKSTICK_VENDOR_ID) &&
                (desc.idProduct == BLINKSTICK_PRODUCT_ID))
        {
            cInfoDom("blinkstick") << "Blinkstick detected";
            int ret = libusb_open(device, handle);
            if (ret < 0)
            {
                cErrorDom("blinkstick") << "Unable to open device";
                return false;
            }

            ret = libusb_get_string_descriptor_ascii(*handle, desc.iSerialNumber,
                                                     serial, 255);
            if (ret < 0)
            {
                cErrorDom("blinkstick") << "Unable to retrieve serial";
                return false;
            }
            else
                return true;
        }
    }

    return false;
}

