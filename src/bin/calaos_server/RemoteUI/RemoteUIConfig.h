/******************************************************************************
 **  Copyright (c) 2006-2025, Calaos. All Rights Reserved.
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
#ifndef REMOTEUICONFIG_H
#define REMOTEUICONFIG_H

#include "Utils.h"
#include "RemoteUI.h"
#include "TinyXML/tinyxml.h"
#include <memory>
#include <vector>

using namespace Utils;

namespace Calaos
{

class RemoteUIConfigParser
{
public:
    // Parse RemoteUIs from XML configuration
    static std::vector<std::shared_ptr<RemoteUI>> parseRemoteUIsFromConfig(TiXmlDocument *document);

    // Save RemoteUIs to XML configuration
    static bool saveRemoteUIsToConfig(TiXmlDocument *document,
                                     const std::vector<std::shared_ptr<RemoteUI>> &remote_uis);

    // Helper methods
    static std::shared_ptr<RemoteUI> parseRemoteUIElement(TiXmlElement *element);
    static TiXmlElement *createRemoteUIElement(TiXmlDocument *document,
                                              std::shared_ptr<RemoteUI> remote_ui);

    // Parse device info from XML
    static DeviceInfo parseDeviceInfo(TiXmlElement *element);
    static void saveDeviceInfo(TiXmlElement *element, const DeviceInfo &info);

    // Parse RemoteUI config from XML
    static RemoteUIConfig parseRemoteUIConfig(TiXmlElement *element);
    static void saveRemoteUIConfig(TiXmlElement *element, const RemoteUIConfig &config);

    // Parse pages and widgets
    static Json parsePagesFromXML(TiXmlElement *pages_element);
    static void savePagesToXML(TiXmlElement *pages_element, const Json &pages);

private:
    // Helper methods for JSON conversion
    static string jsonToString(const Json &j);
    static Json stringToJson(const string &str);

    // XML manipulation helpers
    static void setElementText(TiXmlElement *element, const string &text);
    static string getElementText(TiXmlElement *element);
    static void addParamElement(TiXmlElement *parent, const string &name, const string &value);
    static string getParamValue(TiXmlElement *parent, const string &name);
};

}

#endif