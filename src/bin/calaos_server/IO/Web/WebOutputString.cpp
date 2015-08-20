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
#include <WebOutputString.h>
#include <WebCtrl.h>
#include <jansson.h>

using namespace Calaos;

REGISTER_IO(WebOutputString)

WebOutputString::WebOutputString(Params &p):
    OutputString(p)
{
    cInfoDom("output") << "WebOutputString::WebOutputString()";  
}

WebOutputString::~WebOutputString()
{
    cInfoDom("output") << "WebOutputString::~WebOutputString()";
}


void WebOutputString::readValue()
{
  // Read the value
    value = WebCtrl::Instance(get_params()).getValue(get_param("path"));
    emitChange();
}


void WebOutputString::set_value_real(string val)
{
    cInfoDom("output") << "Set new string value " << val;
    WebCtrl::Instance(get_params()).setValue(val);
}
