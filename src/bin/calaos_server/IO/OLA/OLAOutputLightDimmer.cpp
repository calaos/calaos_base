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
#include "OLAOutputLightDimmer.h"
#include "IOFactory.h"
#include "OLACtrl.h"

REGISTER_IO(OLAOutputLightDimmer)

OLAOutputLightDimmer::OLAOutputLightDimmer(Params &p):
    OutputLightDimmer(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("OLAOutputLightDimmer");
    ioDoc->descriptionSet(_("DMX Light dimmer using OLA (Open Lighting Architecture)"));
    ioDoc->linkAdd("OLA", _("http://www.openlighting.org"));
    ioDoc->paramAddInt("universe", _("OLA universe to control"), 0, 9999, true);
    ioDoc->paramAddInt("channel", _("DMX channel to control"), 0, 512, true);

    OLACtrl::Instance(get_param("universe"));

    cDebugDom("output") << get_param("id") << ": Ok";
}

bool OLAOutputLightDimmer::set_value_real(int val)
{
    int channel = 0;
    Utils::from_string(get_param("channel"), channel);

    OLACtrl::Instance(get_param("universe"))->setValue(channel, val);

    return true;
}
