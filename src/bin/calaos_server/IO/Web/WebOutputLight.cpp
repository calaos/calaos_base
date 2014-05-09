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
#include <WebOutputLight.h>
#include <WebCtrl.h>
#include <jansson.h>

using namespace Calaos;

REGISTER_OUTPUT(WebOutputLight)

WebOutputLight::WebOutputLight(Params &p):
    OutputLight(p)
{
    cInfoDom("output") << "WebOutputLight::WebOutputLight()";  
}

WebOutputLight::~WebOutputLight()
{
    cInfoDom("output") << "WebOutputLight::~WebOutputLight()";
}


void WebOutputLight::readValue()
{
  // Read the value

}

bool WebOutputLight::set_value_real(bool val)
{
    string on_value = "on";
    string off_value = "off";

    if (get_params().Exists("on_value"))
        on_value = get_param("on_value");

    if (get_params().Exists("off_value"))
        off_value = get_param("off_value");


    val ? WebCtrl::Instance(get_params()).setValue(on_value) :
        WebCtrl::Instance(get_params()).setValue(off_value);

    return true;
}
