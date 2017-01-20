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
#include <WebOutputString.h>
#include <WebCtrl.h>
#include <jansson.h>

using namespace Calaos;

REGISTER_IO(WebOutputString)

WebOutputString::WebOutputString(Params &p):
    OutputString(p)
{
    ioDoc->friendlyNameSet("WebOutputString");
    ioDoc->descriptionSet(_("String output written to a web document or URL"));
    ioDoc->paramAdd("url", _("URL where to POST the document to. The POST request is associated "
                             "with the data field if not void. When no data is provided, "
                             "Calaos substitutes __##VALUE##__ string with the value to send. For example "
                             "if the url is http://example.com/api?value=__##VALUE##__ the url post will be :\n"
                             "http://example.com/api?value=20.3\nThe url is encoded before being sent."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data", _("The document send when posting data. This value can be void, in, that case the value "
                              "is substituted in the url, otherwise the __##VALUE##__ contained in data is substituted with "
                              "with the value to be sent."),
                    IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("data_type", _("The HTTP header Content-Type used when posting the document. "
                                   "It depends on the website, but you can use application/json "
                                   "application/xml as correct values."),
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

    cInfoDom("output") << "WebOutputString::WebOutputString()";  
}

WebOutputString::~WebOutputString()
{
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
