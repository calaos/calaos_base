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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ListeRule.h"
#include "WebOutputAnalog.h"
#include "WebCtrl.h"
#include "jansson.h"
#include "IOFactory.h"

using namespace Calaos;

REGISTER_IO(WebOutputAnalog)

WebOutputAnalog::WebOutputAnalog(Params &p):
    OutputAnalog(p)
{
    ioDoc->friendlyNameSet("WebOutputAnalog");
    ioDoc->descriptionSet(_("Analog output in a web request"));
    docBase.initDoc(ioDoc);

    cInfoDom("input") << "WebOutputAnalog::WebOutputAnalog()";
    Calaos::StartReadRules::Instance().addIO();
}

WebOutputAnalog::~WebOutputAnalog()
{
}

void WebOutputAnalog::readValue()
{
    string v = WebCtrl::Instance(get_params()).getValue(get_param("path"));
    if (Utils::is_of_type<double>(v))
    {
        Utils::from_string(v, value);
    }
}

void WebOutputAnalog::set_value_real(double val)
{
    cInfoDom("output") << "Set new double value " << val;
    WebCtrl::Instance(get_params()).setValue(Utils::to_string(val));
}


