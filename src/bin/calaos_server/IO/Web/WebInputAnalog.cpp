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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ListeRule.h>
#include <WebInputAnalog.h>
#include <WebCtrl.h>
#include <jansson.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_INPUT(WebInputAnalog)

WebInputAnalog::WebInputAnalog(Params &p):
    InputAnalog(p)
{
    cInfoDom("input") << "WebInputAnalog::WebInputAnalog()";

    // Add input to WebCtrl instance
    WebCtrl::Instance(p).Add(frequency, [=]()
    {
        readValue();
        Calaos::StartReadRules::Instance().ioRead();
    });
}

WebInputAnalog::~WebInputAnalog()
{
    cInfoDom("input") << "WebInputAnalog::~WebInputAnalog()";
}


void WebInputAnalog::readValue()
{
  // Read the value
    value = WebCtrl::Instance(get_params()).getValueDouble(get_param("path"));
    emitChange();
}


