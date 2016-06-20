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
#include <WebOutputLightRGB.h>
#include <WebCtrl.h>
#include <jansson.h>

namespace Calaos {

REGISTER_IO(WebOutputLightRGB)

WebOutputLightRGB::WebOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    ioDoc->friendlyNameSet("WebOutputLightRGB");
    ioDoc->descriptionSet(_("RGB value written to a web document or URL"));
    ioDoc->paramAdd("url", _("URL where to POST the document to. The POST request is associated "
                             "with the data field if not void. When no data is provided, "
                             "Calaos substitutes __##VALUE##__ string with the value to send. For example "
                             "if the url is http://example.com/api?value=__##VALUE##__ the url post will be :\n"
                             "http://example.com/api?value=20.3\nThe url is encoded before being sent.\n"
                             "If the URL is of type file:// the data is written to a file."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data", _("The document send when posting data. This value can be void, in, that case the value "
                              "is substituted in the url, otherwise the __##VALUE##__ contained in data is substituted with "
                              "with the value to be sent. Value sent has #RRGGBB format or RRGGBB depending on raw_value boolean"),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("The HTTP header Content-Type used when posting the document. "
                                   "It depends on the website, but you can use application/json "
                                   "application/xml as correct values."),
                    IODoc::TYPE_STRING, true);

    ioDoc->paramAdd("file_type",_("File type of the document. Values can be xml, json or text."),
                    IODoc::TYPE_STRING, true);

    ioDoc->paramAdd("raw_value", _("RGB value has #RRGGBB. Sometimes some web api take only RRGGBB"
                                   "format. If raw_value is true, the # in front of the line is"
                                   "removed. The default value for this parameter is false."),
                    IODoc::TYPE_BOOL, false);

    if (get_param("raw_value") == "true")
        raw_value = true;
    else
        raw_value = false;
        
    
    cInfoDom("output") << "WebOutputLightRGB::WebOutputLightRGB()";
}

WebOutputLightRGB::~WebOutputLightRGB()
{
    cInfoDom("output") << "WebOutputLightRGB::~WebOutputLightRGB()";
}


void WebOutputLightRGB::readValue()
{
  // Read the value

}

void WebOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    std::string cStr = c.toString();
    if (raw_value && cStr[0] == '#')
            cStr.erase(0, 1);

    WebCtrl::Instance(get_params()).setValue(cStr);
}

}
