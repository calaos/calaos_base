/******************************************************************************
 **  Copyright (c) 2006-2016, Calaos. All Rights Reserved.
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
#include "TeleinfoInputAnalog.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(TeleinfoInputAnalog)

TeleinfoInputAnalog::TeleinfoInputAnalog(Params &p):
    InputAnalog(p)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("TeleinfoInputAnalog");
    ioDoc->descriptionSet(_("Analog measurement retrieved from Teleinfo informations."));

    ioDoc->paramAdd("port", _("port on which to get Teleinfo information usually a serial port like /dev/ttyS0 or /dev/ttyAMA0"), IODoc::TYPE_STRING, true);

    Params list = {{"ISOUSC", _("Subscribed intensity. (in Ampere)")},
                   {"BASE", _("Index if optione BASE is subscribed. (in Watt-hour)")},
                   {"HCHC",_("Index \"Heures Creuses\" if this option is subscribed. (in Watt-hour)")},
                   {"HCHP",_("Index \"Heures Pleine\" if option \"Heures Creuses\" is subscribed. (in Watt-hour)")},
                   {"EJPHN",_("Index \"Heures Normales\" if otion \"EJP\" is subscribed. (in Watt-hour)")},
                   {"EJPHPM",_("Index \"Heures de pointe mobile\" if otion \"EJP\" is subscribed. (in Watt-hour)")},
                   {"BBRHCJB",_("Index \"Heures creuses jours bleues\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"BBRHPJB",_("Index \"Heures pleines jours bleues\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"BBRHCJW",_("Index \"Heures creuses jours blancs\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"BBRHPJW",_("Index \"Heures pleines jours blancs\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"BBRHCJR",_("Index \"Heures creuses jours rouges\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"BBRHPJR",_("Index \"Heures pleines jours rouges\" if otion \"Tempo\" is subscribed. (in Watt-hour)")},
                   {"IINST",_("Instant intensity. (in Ampere)")},
                   {"IMAX",_("Maximal intensity. (in Ampere)")},
                   {"PAPP",_("Apparent power. (in Volt-ampere)")}};

    ioDoc->paramAddList("value", _("All theses values are reported by the Teleinfo equipment as double."), true, list, "PAPP");

    cDebugDom("teleinfo") << "Register IO for " << param["value"];
    TeleinfoCtrl::Instance(get_params()).registerIO(param["value"], [=]()
    {
      cDebugDom("teleinfo") << "Read Value : " << param["value"];
      readValue();
    });

}

TeleinfoInputAnalog::~TeleinfoInputAnalog()
{
}

void TeleinfoInputAnalog::readValue()
{
    // Read the value
    string teleinfo_value = get_param("value");

    string sv = TeleinfoCtrl::Instance(get_params()).getValue(teleinfo_value);

    cDebugDom("teleinfo") << "sv : " << sv;

    double v;
    if (sv.empty() || !Utils::is_of_type<double>(sv))
        return;
    Utils::from_string(sv, v);

    if (v != value)
    {
        value = v;
        emitChange();
    }
}
