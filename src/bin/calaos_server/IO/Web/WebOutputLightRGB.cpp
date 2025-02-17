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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ListeRule.h"
#include "WebOutputLightRGB.h"
#include "WebCtrl.h"
#include "jansson.h"

using namespace Calaos;

REGISTER_IO(WebOutputLightRGB)

WebOutputLightRGB::WebOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    ioDoc->friendlyNameSet("WebOutputLightRGB");
    ioDoc->descriptionSet(_("RGB value written to a web document or URL"));
    docBase.initDoc(ioDoc, true);

    ioDoc->paramAdd("raw_value", _("RGB value has #RRGGBB. Sometimes some web api take only RRGGBB"
                                   "format. If raw_value is true, the # in front of the line is"
                                   "removed. The default value for this parameter is false."),
                    IODoc::TYPE_BOOL, false);

    cInfoDom("output") << "WebOutputLightRGB::WebOutputLightRGB()";
}

WebOutputLightRGB::~WebOutputLightRGB()
{
}


void WebOutputLightRGB::readValue()
{
  // Read the value
}

void WebOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    string cStr = c.toString();
    if (get_param("raw_value") == "true" && cStr[0] == '#')
            cStr.erase(0, 1);

    WebCtrl::Instance(get_params()).setValue(cStr);
}
