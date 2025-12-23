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
#include "RemoteUI.h"
#include "IOFactory.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>

static const char *TAG = "remote_ui";

using namespace Calaos;

REGISTER_IO(RemoteUI)

RemoteUI::RemoteUI(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    is_online(false),
    is_provisioned(false)
{
    ioDoc->friendlyNameSet("RemoteUI");
    ioDoc->descriptionBaseSet(_("Remote UI device. Represents a remote user interface device. Some actions are available to control the device from rules."));

    Params io_devtype = {{ "waveshare-esp32-p4-86-panel", _("Waveshare ESP32-P4-86-Panel") },
                         { "luckfox-pico-86-panel", _("Luckfox Luckfox-Pico-86-Panel") }
                        };

    ioDoc->paramAddList("device_type", _("Device model"), true, io_devtype, "waveshare-esp32-p4-86-panel");
    ioDoc->paramAdd("provisioning_code", _("Provisioning code for first time setup"), IODoc::TYPE_STRING, true);

    set_param("gui_type", "remote_ui");

    readConfig();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Ok";
}

RemoteUI::~RemoteUI()
{
}

void RemoteUI::readConfig()
{
    if (!get_params().Exists("visible"))
        set_param("visible", "false");

    // Check if provisioned
    is_provisioned = !get_param("device_secret").empty() && !get_param("auth_token").empty();
}

bool RemoteUI::LoadFromXml(TiXmlElement *node)
{
    if (!IOBase::LoadFromXml(node))
        return false;

    // Load device_info
    TiXmlElement *device_info_elem = node->FirstChildElement("calaos:device_info");
    if (device_info_elem)
    {
        device_info = Json::object();

        TiXmlAttribute *attr = device_info_elem->FirstAttribute();
        while (attr)
        {
            device_info[attr->Name()] = attr->ValueStr();
            attr = attr->Next();
        }
    }

    // Load pages
    TiXmlElement *pages_elem = node->FirstChildElement("calaos:pages");
    if (pages_elem)
    {
        pages = Json::array();

        for (TiXmlElement *page_elem = pages_elem->FirstChildElement("calaos:page");
             page_elem;
             page_elem = page_elem->NextSiblingElement("calaos:page"))
        {
            Json page = Json::object();

            TiXmlAttribute *attr = page_elem->FirstAttribute();
            while (attr)
            {
                page[attr->Name()] = attr->ValueStr();
                attr = attr->Next();
            }

            // Load widgets for this page
            Json widgets = Json::array();
            for (TiXmlElement *widget_elem = page_elem->FirstChildElement("calaos:widget");
                 widget_elem;
                 widget_elem = widget_elem->NextSiblingElement("calaos:widget"))
            {
                Json widget = Json::object();

                attr = widget_elem->FirstAttribute();
                while (attr)
                {
                    // Convert numeric attributes
                    string attr_name = attr->Name();
                    string attr_value = attr->ValueStr();

                    if (attr_name == "x" || attr_name == "y")
                        widget[attr_name] = std::stoi(attr_value);
                    else
                        widget[attr_name] = attr_value;

                    attr = attr->Next();
                }

                //Only add widget if it has an io_id, a type and x/y positions
                if (widget.contains("io_id") && widget.contains("type") &&
                    widget.contains("x") && widget.contains("y"))
                    widgets.push_back(widget);
                else
                    cWarningDom(TAG) << "RemoteUI(" << get_param("id") << "): Ignoring widget with missing required attributes";
            }

            page["widgets"] = widgets;
            pages.push_back(page);
        }
    }

    extractReferencedIOs();

    return true;
}

bool RemoteUI::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:remote_ui");

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    // Save device_info
    if (!device_info.empty())
    {
        TiXmlElement *device_info_elem = new TiXmlElement("calaos:device_info");

        for (auto it = device_info.begin(); it != device_info.end(); ++it)
        {
            if (it.value().is_string())
                device_info_elem->SetAttribute(it.key(), it.value().get<string>());
        }

        node->LinkEndChild(device_info_elem);
    }

    // Save pages
    if (!pages.empty() && pages.is_array())
    {
        TiXmlElement *pages_elem = new TiXmlElement("calaos:pages");

        for (const auto &page : pages)
        {
            TiXmlElement *page_elem = new TiXmlElement("calaos:page");

            for (auto it = page.begin(); it != page.end(); ++it)
            {
                if (it.key() == "widgets")
                    continue;

                if (it.value().is_string())
                    page_elem->SetAttribute(it.key(), it.value().get<string>());
            }

            // Save widgets
            if (page.contains("widgets") && page["widgets"].is_array())
            {
                for (const auto &widget : page["widgets"])
                {
                    TiXmlElement *widget_elem = new TiXmlElement("calaos:widget");

                    for (auto it = widget.begin(); it != widget.end(); ++it)
                    {
                        if (it.value().is_string())
                            widget_elem->SetAttribute(it.key(), it.value().get<string>());
                        else if (it.value().is_number_integer())
                            widget_elem->SetAttribute(it.key(), std::to_string(it.value().get<int>()));
                    }

                    page_elem->LinkEndChild(widget_elem);
                }
            }

            pages_elem->LinkEndChild(page_elem);
        }

        cnode->LinkEndChild(pages_elem);
    }

    node->LinkEndChild(cnode);

    return true;
}

bool RemoteUI::set_value(string val)
{
    // Parse command string
    // Format: "command args..."
    // Examples: "set_brightness 100", "set_page 2", "show_notif Hello!"

    size_t space_pos = val.find(' ');
    string command;
    string args;

    if (space_pos != string::npos)
    {
        command = val.substr(0, space_pos);
        args = val.substr(space_pos + 1);
    }
    else
    {
        command = val;
    }

    cDebugDom(TAG) << "RemoteUI(" << get_param("id") << "): command=" << command << " args=" << args;

    if (command == "set_brightness")
    {
        try
        {
            int brightness = std::stoi(args);
            setBrightness(brightness);
        }
        catch (const std::exception &e)
        {
            cErrorDom(TAG) << "RemoteUI: Invalid brightness value: " << args;
        }
    }
    else if (command == "set_page")
    {
        setPage(args);
    }
    else if (command == "show_notif")
    {
        showNotification(args);
    }
    else
    {
        cWarningDom(TAG) << "RemoteUI: Unknown command: " << command;
    }

    return true;
}

void RemoteUI::setOnline(bool online)
{
    is_online = online;
    if (online)
        updateLastSeen();
}

void RemoteUI::updateLastSeen()
{
    last_seen = std::chrono::system_clock::now();
}

string RemoteUI::generateRandomSecret(size_t length) const
{
    const size_t MAX_SECRET_LENGTH = 1024;
    if (length > MAX_SECRET_LENGTH)
    {
        cErrorDom(TAG) << "Requested secret length too large: " << length;
        return "";
    }

    std::vector<unsigned char> buffer(length);
    if (RAND_bytes(buffer.data(), length) != 1)
    {
        cErrorDom(TAG) << "Failed to generate random bytes for device secret";
        return "";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }

    return oss.str();
}

void RemoteUI::generateDeviceSecret()
{
    string secret = generateRandomSecret(32);
    set_param("device_secret", secret);

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Generated new device secret";
}

bool RemoteUI::validateHMAC(const string &token, const string &timestamp, const string &nonce, const string &hmac)
{
    string device_secret = get_param("device_secret");
    if (device_secret.empty())
        return false;

    string message = token + ":" + timestamp + ":" + nonce;

    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int result_len;

    HMAC(EVP_sha256(),
         device_secret.c_str(), device_secret.length(),
         reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
         result, &result_len);

    std::ostringstream oss;
    for (unsigned int i = 0; i < result_len; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(result[i]);
    }

    return oss.str() == hmac;
}

void RemoteUI::extractReferencedIOs()
{
    referenced_ios.clear();

    if (pages.is_array())
    {
        for (const auto &page : pages)
        {
            if (page.contains("widgets") && page["widgets"].is_array())
            {
                for (const auto &widget : page["widgets"])
                {
                    if (widget.contains("io_id") && widget["io_id"].is_string())
                    {
                        referenced_ios.insert(widget["io_id"]);
                    }
                }
            }
        }
    }

    cDebugDom(TAG) << "RemoteUI(" << get_param("id") << "): Extracted " << referenced_ios.size() << " referenced IOs";
}

bool RemoteUI::hasReferencedIO(const string &io_id) const
{
    return referenced_ios.find(io_id) != referenced_ios.end();
}

Json RemoteUI::getProvisioningResponse()
{
    Json response;
    response["status"] = "accepted";
    response["device_id"] = get_param("id");
    response["auth_token"] = get_param("auth_token");
    response["device_secret"] = get_param("device_secret");

    Json server_config;
    server_config["websocket_url"] = "ws://localhost:5454/api/v3/remote_ui/ws";
    server_config["sync_interval"] = 1000;
    response["server_config"] = server_config;

    Json remote_ui_config;
    remote_ui_config["name"] = get_param("name");
    remote_ui_config["pages"] = pages;
    response["remote_ui_config"] = remote_ui_config;

    return response;
}

Json RemoteUI::getRemoteUIIOStatesMessage(const std::map<string, Json> &io_states)
{
    Json data = Json::array();
    for (const string &io_id : referenced_ios)
    {
        auto it = io_states.find(io_id);
        if (it != io_states.end())
        {
            data.push_back(it->second);
        }
    }

    return data;
}

Json RemoteUI::getRemoteUIConfigMessage()
{
    Json config;
    config["name"] = get_param("name");
    config["room"] = get_param("room");
    config["theme"] = get_param("theme");
    config["brightness"] = std::stoi(get_param("brightness"));
    config["timeout"] = std::stoi(get_param("timeout"));
    config["pages"] = pages;

    return config;
}

bool RemoteUI::setBrightness(int brightness)
{
    if (brightness < 0 || brightness > 100)
    {
        cWarningDom(TAG) << "RemoteUI: Invalid brightness value: " << brightness;
        return false;
    }

    set_param("brightness", Utils::to_string(brightness));

    // Emit event to notify WebSocket handler
    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Brightness set to " << brightness;
    return true;
}

int RemoteUI::getBrightness()
{
    int brightness = 100; // Default value
    Utils::from_string(get_param("brightness"), brightness);
    return brightness;
}

bool RemoteUI::setPage(const string &page_id)
{
    // Emit event to notify WebSocket handler
    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Page set to " << page_id;
    return true;
}

bool RemoteUI::showNotification(const string &message)
{
    // Emit event to notify WebSocket handler
    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Notification queued: " << message;
    return true;
}

void RemoteUI::emitChange()
{
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", cmd_state } });
}
