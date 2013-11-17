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
#include <cJSON.h>

using namespace Calaos;

WebAnalogIn::WebAnalogIn(Params &p):
         InputAnalog(p)
{
        Utils::logger("input") << Priority::INFO << "WebAnalogIn::WebAnalogIn()" << log4cpp::eol;

        //web_variable_name = get_param("name");
        WebCtrl::Instance().Add(p);

        //read value when calaos_server is started
        readValue();
        Calaos::StartReadRules::Instance().ioRead();
}

WebAnalogIn::~WebAnalogIn()
{

        Utils::logger("input") << Priority::INFO << "WebAnalogIn::~WebAnalogIn()" << log4cpp::eol;
}


void WebAnalogIn::readValue()
{
        ifstream::pos_type size;
        char *content;

        ifstream file ("/tmp/calaos-test", ios::in|ios::ate);
        if (file.is_open())
        {
                cJSON *root;
                cJSON *var;

                size = file.tellg();
                content = new char [size];
                file.seekg (0, ios::beg);
                file.read (content, size);
                file.close();

                root = cJSON_Parse(content);
                if (root)
                {
                        var = cJSON_GetObjectItem(root, "INDEX_C2");
                        if (var)
                        {
                                value = var->valuedouble;
                                Utils::logger("input") << Priority::DEBUG << "WebAnalogIn value : "<< value << log4cpp::eol;
                                emitChange();
                        }
                }
                delete[] content;
        }
}


