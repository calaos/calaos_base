/******************************************************************************
**  Copyright (c) 2007-2013, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
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
#include <WebAnalogIn.h>
#include <WebCtrl.h>
#include <jansson.h>

using namespace Calaos;

WebAnalogIn::WebAnalogIn(Params &p):
         InputAnalog(p)
{
        cInfoDom("input") << "WebAnalogIn::WebAnalogIn()";

        WebCtrl::Instance(p).Add(frequency);

        //read value when calaos_server is started
        readValue();
        Calaos::StartReadRules::Instance().ioRead();
}

WebAnalogIn::~WebAnalogIn()
{
        cInfoDom("input") << "WebAnalogIn::~WebAnalogIn()";
}


void WebAnalogIn::readValue()
{
        value = WebCtrl::Instance(get_params()).getValue(get_param("path"));
        emitChange();
}


