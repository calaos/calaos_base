/******************************************************************************
 **  Copyright (c) 2006-2018, Calaos. All Rights Reserved.
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
#include "WebInputString.h"
#include "WebCtrl.h"
#include "jansson.h"

using namespace Calaos;

REGISTER_IO(WebInputString)

WebInputString::WebInputString(Params &p):
    InputString(p)
{
    ioDoc->friendlyNameSet("WebInputString");
    ioDoc->descriptionSet(_("String input providing from a web document"));
    docBase.initDoc(ioDoc);

    cInfoDom("input") << "WebInputString::WebInputString()";
    Calaos::StartReadRules::Instance().addIO();

    if (!get_param("path").empty())
    {
        // Add input to WebCtrl instance
        WebCtrl::Instance(p).Add(get_param("path"), frequency, [=]()
        {
            readValue();
            Calaos::StartReadRules::Instance().ioRead();
        });
    }
    cInfoDom("input") << "Period : " << frequency;
}

WebInputString::~WebInputString()
{
    WebCtrl::Instance(get_params()).Del(get_param("path"));
}

void WebInputString::readValue()
{
    auto v = WebCtrl::Instance(get_params()).getValue(get_param("path"));
    if (v != value)
    {
        value = v;
        cInfoDom("input") << "Read string value : " << value;
        emitChange();
    }
}
