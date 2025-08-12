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
#include "RemoteUIConfig.h"

using namespace Calaos;

std::vector<std::shared_ptr<RemoteUI>> RemoteUIConfigParser::parseRemoteUIsFromConfig(TiXmlDocument *document)
{
    std::vector<std::shared_ptr<RemoteUI>> remote_uis;

    if (!document)
        return remote_uis;

    TiXmlElement *root = document->FirstChildElement("calaos:config");
    if (!root)
        return remote_uis;

    TiXmlElement *home = root->FirstChildElement("calaos:home");
    if (!home)
        return remote_uis;

    TiXmlElement *remote_uis_element = home->FirstChildElement("calaos:remote_uis");
    if (!remote_uis_element)
    {
        cInfo() << "RemoteUIConfigParser: No remote_uis section found in config (normal for first run)";
        return remote_uis;
    }

    for (TiXmlElement *remote_ui_element = remote_uis_element->FirstChildElement("calaos:remote_ui");
         remote_ui_element;
         remote_ui_element = remote_ui_element->NextSiblingElement("calaos:remote_ui"))
    {
        auto remote_ui = parseRemoteUIElement(remote_ui_element);
        if (remote_ui)
        {
            remote_uis.push_back(remote_ui);
            cInfo() << "RemoteUIConfigParser: Parsed RemoteUI " << remote_ui->getId();
        }
    }

    cInfo() << "RemoteUIConfigParser: Parsed " << remote_uis.size() << " RemoteUIs from config";
    return remote_uis;
}

bool RemoteUIConfigParser::saveRemoteUIsToConfig(TiXmlDocument *document,
                                                const std::vector<std::shared_ptr<RemoteUI>> &remote_uis)
{
    if (!document)
        return false;

    TiXmlElement *root = document->FirstChildElement("calaos:config");
    if (!root)
        return false;

    TiXmlElement *home = root->FirstChildElement("calaos:home");
    if (!home)
        return false;

    // Remove existing remote_uis section
    TiXmlElement *existing_remote_uis = home->FirstChildElement("calaos:remote_uis");
    if (existing_remote_uis)
        home->RemoveChild(existing_remote_uis);

    // Create new remote_uis section
    if (!remote_uis.empty())
    {
        TiXmlElement *remote_uis_element = new TiXmlElement("calaos:remote_uis");
        home->LinkEndChild(remote_uis_element);

        for (const auto &remote_ui : remote_uis)
        {
            if (remote_ui)
            {
                TiXmlElement *remote_ui_element = createRemoteUIElement(document, remote_ui);
                if (remote_ui_element)
                {
                    remote_uis_element->LinkEndChild(remote_ui_element);
                }
            }
        }
    }

    cInfo() << "RemoteUIConfigParser: Saved " << remote_uis.size() << " RemoteUIs to config";
    return true;
}

std::shared_ptr<RemoteUI> RemoteUIConfigParser::parseRemoteUIElement(TiXmlElement *element)
{
    if (!element)
        return nullptr;

    auto remote_ui = std::make_shared<RemoteUI>();

    // Parse basic attributes
    string id = element->Attribute("id") ? element->Attribute("id") : "";
    if (id.empty())
    {
        cWarning() << "RemoteUIConfigParser: RemoteUI element missing id attribute";
        return nullptr;
    }

    // Parse child elements
    string provisioning_code = getElementText(element->FirstChildElement("calaos:provisioning_code"));
    string auth_token = getElementText(element->FirstChildElement("calaos:auth_token"));
    string device_secret = getElementText(element->FirstChildElement("calaos:device_secret"));
    string mac_address = getElementText(element->FirstChildElement("calaos:mac_address"));

    // Parse device info
    TiXmlElement *device_info_element = element->FirstChildElement("calaos:device_info");
    DeviceInfo device_info;
    if (device_info_element)
        device_info = parseDeviceInfo(device_info_element);

    // Parse config
    RemoteUIConfig config = parseRemoteUIConfig(element);

    // Create RemoteUI object with parsed data
    Json remote_ui_json;
    remote_ui_json["id"] = id;
    remote_ui_json["provisioning_code"] = provisioning_code;
    remote_ui_json["auth_token"] = auth_token;
    remote_ui_json["device_secret"] = device_secret;
    remote_ui_json["mac_address"] = mac_address;
    remote_ui_json["device_info"] = device_info.toJson();
    remote_ui_json["config"] = config.toJson();
    remote_ui_json["is_online"] = false;
    remote_ui_json["is_provisioned"] = !provisioning_code.empty();

    // Parse timestamps if available
    string provisioned_at = getElementText(element->FirstChildElement("calaos:provisioned_at"));
    if (!provisioned_at.empty())
        remote_ui_json["provisioned_at"] = provisioned_at;

    string last_seen = getElementText(element->FirstChildElement("calaos:last_seen"));
    if (!last_seen.empty())
        remote_ui_json["last_seen"] = last_seen;

    remote_ui->fromJson(remote_ui_json);
    return remote_ui;
}

TiXmlElement *RemoteUIConfigParser::createRemoteUIElement(TiXmlDocument *document,
                                                         std::shared_ptr<RemoteUI> remote_ui)
{
    if (!document || !remote_ui)
        return nullptr;

    TiXmlElement *element = new TiXmlElement("calaos:remote_ui");
    element->SetAttribute("id", remote_ui->getId().c_str());

    // Add child elements
    if (!remote_ui->getProvisioningCode().empty())
    {
        TiXmlElement *code_element = new TiXmlElement("calaos:provisioning_code");
        setElementText(code_element, remote_ui->getProvisioningCode());
        element->LinkEndChild(code_element);
    }

    if (!remote_ui->getAuthToken().empty())
    {
        TiXmlElement *token_element = new TiXmlElement("calaos:auth_token");
        setElementText(token_element, remote_ui->getAuthToken());
        element->LinkEndChild(token_element);
    }

    if (!remote_ui->getDeviceSecret().empty())
    {
        TiXmlElement *secret_element = new TiXmlElement("calaos:device_secret");
        setElementText(secret_element, remote_ui->getDeviceSecret());
        element->LinkEndChild(secret_element);
    }

    if (!remote_ui->getMacAddress().empty())
    {
        TiXmlElement *mac_element = new TiXmlElement("calaos:mac_address");
        setElementText(mac_element, remote_ui->getMacAddress());
        element->LinkEndChild(mac_element);
    }

    // Add device info
    TiXmlElement *device_info_element = new TiXmlElement("calaos:device_info");
    saveDeviceInfo(device_info_element, remote_ui->getDeviceInfo());
    element->LinkEndChild(device_info_element);

    // Add config parameters
    const RemoteUIConfig &config = remote_ui->getConfig();

    addParamElement(element, "name", config.name);
    addParamElement(element, "room", config.room);
    addParamElement(element, "theme", config.theme);
    addParamElement(element, "brightness", Utils::to_string(config.brightness));
    addParamElement(element, "timeout", Utils::to_string(config.timeout));

    // Add pages
    if (!config.pages.empty())
    {
        TiXmlElement *pages_element = new TiXmlElement("calaos:pages");
        savePagesToXML(pages_element, config.pages);
        element->LinkEndChild(pages_element);
    }

    return element;
}

DeviceInfo RemoteUIConfigParser::parseDeviceInfo(TiXmlElement *element)
{
    DeviceInfo info;

    if (!element)
        return info;

    info.type = getParamValue(element, "type");
    info.manufacturer = getParamValue(element, "manufacturer");
    info.model = getParamValue(element, "model");
    info.firmware = getParamValue(element, "firmware");
    info.mac_address = getParamValue(element, "mac_address");

    // Parse capabilities JSON if present
    string capabilities_str = getParamValue(element, "capabilities");
    if (!capabilities_str.empty())
    {
        info.capabilities = stringToJson(capabilities_str);
    }

    return info;
}

void RemoteUIConfigParser::saveDeviceInfo(TiXmlElement *element, const DeviceInfo &info)
{
    if (!element)
        return;

    addParamElement(element, "type", info.type);
    addParamElement(element, "manufacturer", info.manufacturer);
    addParamElement(element, "model", info.model);
    addParamElement(element, "firmware", info.firmware);
    addParamElement(element, "mac_address", info.mac_address);

    if (!info.capabilities.empty())
    {
        addParamElement(element, "capabilities", jsonToString(info.capabilities));
    }
}

RemoteUIConfig RemoteUIConfigParser::parseRemoteUIConfig(TiXmlElement *element)
{
    RemoteUIConfig config;

    if (!element)
        return config;

    config.name = getParamValue(element, "name");
    config.room = getParamValue(element, "room");
    config.theme = getParamValue(element, "theme");

    string brightness_str = getParamValue(element, "brightness");
    if (!brightness_str.empty() && !Utils::from_string(brightness_str, config.brightness))
        config.brightness = 80;
    else if (brightness_str.empty())
        config.brightness = 80;

    string timeout_str = getParamValue(element, "timeout");
    if (!timeout_str.empty() && !Utils::from_string(timeout_str, config.timeout))
        config.timeout = 300;
    else if (timeout_str.empty())
        config.timeout = 300;

    // Parse pages
    TiXmlElement *pages_element = element->FirstChildElement("calaos:pages");
    if (pages_element)
        config.pages = parsePagesFromXML(pages_element);

    return config;
}

void RemoteUIConfigParser::saveRemoteUIConfig(TiXmlElement *element, const RemoteUIConfig &config)
{
    if (!element)
        return;

    addParamElement(element, "name", config.name);
    addParamElement(element, "room", config.room);
    addParamElement(element, "theme", config.theme);
    addParamElement(element, "brightness", Utils::to_string(config.brightness));
    addParamElement(element, "timeout", Utils::to_string(config.timeout));

    if (!config.pages.empty())
    {
        TiXmlElement *pages_element = new TiXmlElement("calaos:pages");
        savePagesToXML(pages_element, config.pages);
        element->LinkEndChild(pages_element);
    }
}

Json RemoteUIConfigParser::parsePagesFromXML(TiXmlElement *pages_element)
{
    Json pages = Json::array();

    if (!pages_element)
        return pages;

    for (TiXmlElement *page_element = pages_element->FirstChildElement("calaos:page");
         page_element;
         page_element = page_element->NextSiblingElement("calaos:page"))
    {
        Json page;

        if (page_element->Attribute("id"))
            page["id"] = page_element->Attribute("id");
        if (page_element->Attribute("name"))
            page["name"] = page_element->Attribute("name");

        Json widgets = Json::array();
        for (TiXmlElement *widget_element = page_element->FirstChildElement("calaos:widget");
             widget_element;
             widget_element = widget_element->NextSiblingElement("calaos:widget"))
        {
            Json widget;

            if (widget_element->Attribute("type"))
                widget["type"] = widget_element->Attribute("type");
            if (widget_element->Attribute("io"))
                widget["io"] = widget_element->Attribute("io");
            if (widget_element->Attribute("x"))
            {
                int x = 0;
                if (Utils::from_string(string(widget_element->Attribute("x")), x))
                    widget["x"] = x;
            }
            if (widget_element->Attribute("y"))
            {
                int y = 0;
                if (Utils::from_string(string(widget_element->Attribute("y")), y))
                    widget["y"] = y;
            }
            if (widget_element->Attribute("w"))
            {
                int w = 0;
                if (Utils::from_string(string(widget_element->Attribute("w")), w))
                    widget["w"] = w;
            }
            if (widget_element->Attribute("h"))
            {
                int h = 0;
                if (Utils::from_string(string(widget_element->Attribute("h")), h))
                    widget["h"] = h;
            }

            widgets.push_back(widget);
        }

        page["widgets"] = widgets;
        pages.push_back(page);
    }

    return pages;
}

void RemoteUIConfigParser::savePagesToXML(TiXmlElement *pages_element, const Json &pages)
{
    if (!pages_element || !pages.is_array())
        return;

    for (const auto &page : pages)
    {
        TiXmlElement *page_element = new TiXmlElement("calaos:page");

        if (page.contains("id"))
            page_element->SetAttribute("id", page["id"].get<string>().c_str());
        if (page.contains("name"))
            page_element->SetAttribute("name", page["name"].get<string>().c_str());

        if (page.contains("widgets") && page["widgets"].is_array())
        {
            for (const auto &widget : page["widgets"])
            {
                TiXmlElement *widget_element = new TiXmlElement("calaos:widget");

                if (widget.contains("type"))
                    widget_element->SetAttribute("type", widget["type"].get<string>().c_str());
                if (widget.contains("io"))
                    widget_element->SetAttribute("io", widget["io"].get<string>().c_str());
                if (widget.contains("x"))
                    widget_element->SetAttribute("x", Utils::to_string(widget["x"].get<int>()).c_str());
                if (widget.contains("y"))
                    widget_element->SetAttribute("y", Utils::to_string(widget["y"].get<int>()).c_str());
                if (widget.contains("w"))
                    widget_element->SetAttribute("w", Utils::to_string(widget["w"].get<int>()).c_str());
                if (widget.contains("h"))
                    widget_element->SetAttribute("h", Utils::to_string(widget["h"].get<int>()).c_str());

                page_element->LinkEndChild(widget_element);
            }
        }

        pages_element->LinkEndChild(page_element);
    }
}

// Helper methods implementation

string RemoteUIConfigParser::jsonToString(const Json &j)
{
    return j.dump();
}

Json RemoteUIConfigParser::stringToJson(const string &str)
{
    try
    {
        return Json::parse(str);
    }
    catch (const std::exception &e)
    {
        cWarning() << "RemoteUIConfigParser: Failed to parse JSON: " << e.what();
        return Json{};
    }
}

void RemoteUIConfigParser::setElementText(TiXmlElement *element, const string &text)
{
    if (element)
    {
        TiXmlText *text_node = new TiXmlText(text.c_str());
        element->LinkEndChild(text_node);
    }
}

string RemoteUIConfigParser::getElementText(TiXmlElement *element)
{
    if (element && element->GetText())
        return string(element->GetText());
    return "";
}

void RemoteUIConfigParser::addParamElement(TiXmlElement *parent, const string &name, const string &value)
{
    if (parent && !value.empty())
    {
        TiXmlElement *param_element = new TiXmlElement("calaos:param");
        param_element->SetAttribute("name", name.c_str());
        param_element->SetAttribute("value", value.c_str());
        parent->LinkEndChild(param_element);
    }
}

string RemoteUIConfigParser::getParamValue(TiXmlElement *parent, const string &name)
{
    if (!parent)
        return "";

    for (TiXmlElement *param_element = parent->FirstChildElement("calaos:param");
         param_element;
         param_element = param_element->NextSiblingElement("calaos:param"))
    {
        const char *param_name = param_element->Attribute("name");
        const char *param_value = param_element->Attribute("value");

        if (param_name && param_value && name == param_name)
            return string(param_value);
    }

    return "";
}