/******************************************************************************
 **  Copyright (c) 2007-2015, Calaos. All Rights Reserved.
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
#include "OLAOutputLightRGB.h"
#include "IOFactory.h"
#include "OLACtrl.h"

REGISTER_OUTPUT(OLAOutputLightRGB)

OLAOutputLightRGB::OLAOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("OLAOutputLightRGB");
    ioDoc->descriptionSet(_("RGB Light dimmer using 3 DMX channels with OLA (Open Lighting Architecture)"));
    ioDoc->linkAdd("OLA", _("http://www.openlighting.org"));
    ioDoc->paramAddInt("universe", _("OLA universe to control"), 0, 9999, true);
    ioDoc->paramAddInt("channel_red", _("DMX channel for red to control"), 0, 9999, true);
    ioDoc->paramAddInt("channel_green", _("DMX channel for green to control"), 0, 512, true);
    ioDoc->paramAddInt("channel_blue", _("DMX channel for blue to control"), 0, 512, true);

    OLACtrl::Instance(get_param("universe"));

    cDebugDom("output") << get_param("id") << ": Ok";
}

void OLAOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    int channel_red = 0, channel_green = 0, channel_blue = 0;
    Utils::from_string(get_param("channel_red"), channel_red);
    Utils::from_string(get_param("channel_green"), channel_green);
    Utils::from_string(get_param("channel_blue"), channel_blue);

    if (!s)
        OLACtrl::Instance(get_param("universe"))->setColor(ColorValue::fromRgb(0, 0, 0),
                                                           channel_red,
                                                           channel_green,
                                                           channel_blue);
    else
        OLACtrl::Instance(get_param("universe"))->setColor(c,
                                                           channel_red,
                                                           channel_green,
                                                           channel_blue);
}
