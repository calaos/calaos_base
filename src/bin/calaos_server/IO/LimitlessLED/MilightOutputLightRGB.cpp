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
#include "MilightOutputLightRGB.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(MilightOutputLightRGB)

MilightOutputLightRGB::MilightOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("MilightOutputLightRGB");
    ioDoc->descriptionSet(_("RGB light support for Limitless/Milight RGB bulbs."));
    ioDoc->linkAdd("LimitlessLED", _("http://www.limitlessled.com"));
    ioDoc->paramAdd("host", _("Milight wifi gateway IP address"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Gateway port, default to 8899"), 0, 65535, false);
    ioDoc->paramAddInt("zone", _("Zone to control. Each gateway supports 4 zones."), 0, 4, true);

    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);
    else
        port = DEFAULT_MILIGHT_PORT;
    if (get_params().Exists("zone"))
        Utils::from_string(get_param("zone"), zone);

    milight = new Milight(host, port);
}

MilightOutputLightRGB::~MilightOutputLightRGB()
{
    delete milight;
}

void MilightOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    if (!s)
    {
        milight->sendOffCommand(zone);
    }
    else
    {
        cDebugDom("milight") << "Setting color: " << c.toString();
        ushort micolor = milight->calcMilightColor(c);
        cDebugDom("milight") << "milight color: " << micolor;
        milight->sendColorCommand(zone, micolor);
        EcoreTimer::singleShot(0.2, [=]()
        {
            double luminance = 0.299 * (double)c.getRed() +
                               0.587 * (double)c.getGreen() +
                               0.114 * (double)c.getBlue();
            double v = luminance * 18. / 100.;
            cDebugDom("milight") << "HSL lightness: " << c.getHSLLightness();
            cDebugDom("milight") << "v: " << v;
            milight->sendBrightnessCommand(zone, int(v) + 1);
        });
    }
}
