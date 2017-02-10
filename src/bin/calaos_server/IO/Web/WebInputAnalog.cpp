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

#include <ListeRule.h>
#include <WebInputAnalog.h>
#include <WebCtrl.h>
#include <jansson.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_IO(WebInputAnalog)

WebInputAnalog::WebInputAnalog(Params &p):
    InputAnalog(p)
{
    ioDoc->friendlyNameSet("WebInputAnalog");
    ioDoc->descriptionSet(_("Analog input read from a web document"));
    ioDoc->paramAdd("url", _("URL where to download the document from\n"
                             "If URL begins with / the data is read from the local file"),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("file_type",_("File type of the document. Values can be xml, json or text."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("path",_("The path where to found the value. This value can take multiple values "
                             "depending on the file type. If file_type is JSON, the json file "
                             "downloaded will be read, and the informations will be extracted from "
                             "the path. for example weather[0]/description, try to read the "
                             "description value of the 1 element of the array of the weather object.\n"
                             "If file_type is XML, the path is an xpath expression; Look here for "
                             "syntax : http://www.w3schools.com/xsl/xpath_syntax.asp "
                             "If file_type is TEXT, the downloaded file is returned as "
                             "plain text file, and path must be in the form line/pos/separator "
                             "Line is read, and is split using separator as delimiters "
                             "The value returned is the value at pos in the split list. "
                             "If the separator is not found, the whole line is returned. "
                             "Example the file contains \n"
                             "10.0,10.1,10.2,10.3\n"
                             "20.0,20.1,20.2,20.3\n"
                             "If the path is 2/4/, the value returne wil be 20.3\n"),
                    IODoc::TYPE_STRING, true);


    cInfoDom("input") << "WebInputAnalog::WebInputAnalog()";
    Calaos::StartReadRules::Instance().addIO();

    // Add input to WebCtrl instance
    WebCtrl::Instance(p).Add(get_param("path"), frequency, [=]()
    {
        readValue();
        Calaos::StartReadRules::Instance().ioRead();
    });
}

WebInputAnalog::~WebInputAnalog()
{
    WebCtrl::Instance(get_params()).Del(get_param("path"));
}


void WebInputAnalog::readValue()
{
  // Read the value
    value = WebCtrl::Instance(get_params()).getValueDouble(get_param("path"));
    emitChange();
}


