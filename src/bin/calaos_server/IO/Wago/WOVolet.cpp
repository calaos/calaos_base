/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#include <WOVolet.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(WOVolet)
REGISTER_IO_USERTYPE(WagoOutputShutter, WOVolet)

WOVolet::WOVolet(Params &p):
    WOVoletBase<OutputShutter>(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WOVolet");
    ioDoc->aliasAdd("WagoOutputShutter");
    ioDoc->descriptionSet(_("Simple shutter using wago digital output modules (like 750-1504, ...)"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/750-1504"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("port", _("Wago ethernet port, default to 502"), 0, 65535, false, 502);
    ioDoc->paramAddInt("var_up", _("Digital output address on the PLC for opening the shutter"), 0, 65535, true);
    ioDoc->paramAddInt("var_down", _("Digital output address on the PLC for closing the shutter"), 0, 65535, true);
    ioDoc->paramAdd("wago_841", _("Should be false if PLC is 750-842, true otherwise"), IODoc::TYPE_BOOL, true, "true");
    ioDoc->paramAdd("knx", _("Set to true if output is a KNX device (only for 750-849 with KNX/TP1 module)"), IODoc::TYPE_BOOL, false);

    voletInit();
}
