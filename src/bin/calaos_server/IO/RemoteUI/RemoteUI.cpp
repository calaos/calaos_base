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
#include "CalaosConfig.h"
#include "IOFactory.h"
#include "RemoteUI/HMACAuthenticator.h"
#include "RemoteUI/RemoteUISecurityLimits.h"
#include "RemoteUIManager.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>

static const char *TAG = "remote_ui";

using namespace Calaos;

namespace
{

/*
 * T3.15 — reading back the <calaos:device_info> of the configs written by the
 * broken writer.
 *
 * Until this ticket SaveToXml() attached the element to the node it received,
 * which is the ROOM node (Room::SaveToXml() hands its own element to every IO
 * it owns), and it did so BEFORE appending <calaos:remote_ui>. The element
 * therefore came out as the immediate PREVIOUS SIBLING of the device it
 * describes, where LoadFromXml() — which has always looked inside
 * <calaos:remote_ui>, as the format documents — never found it.
 *
 * Room::LoadFromXml() ignores every element whose name it does not know, so
 * those orphans are still sitting in the io.xml of anyone whose device_info
 * had ever been read once. Nothing else in the format ever writes a
 * <calaos:device_info> under a room, so "the element immediately preceding
 * this <calaos:remote_ui>" identifies its owner with no ambiguity: it works
 * for a room holding several devices (the writer interleaved them
 * device_info(A), remote_ui(A), device_info(B), remote_ui(B)), and a
 * remote_ui preceded by anything else — another IO, or nothing at all —
 * adopts nothing.
 *
 * This is a one shot migration: the value is re-saved in its right place, so
 * the orphan disappears with the next save (Room::SaveToXml() rebuilds the
 * room element from the model, it does not patch the old document).
 */
pugi::xml_node legacyRoomDeviceInfo(const pugi::xml_node &remote_ui_node)
{
    for (pugi::xml_node prev = remote_ui_node.previous_sibling(); prev; prev = prev.previous_sibling())
    {
        //Whitespace/comments between two elements are not the predecessor
        if (prev.type() != pugi::node_element)
            continue;

        if (string(prev.name()) == "calaos:device_info")
            return prev;

        //The nearest preceding element is something else: no orphan for us
        return pugi::xml_node();
    }

    return pugi::xml_node();
}

//What the user needs to find the offending line in io.xml. Every field is
//optional: the widget is being dropped precisely because something is missing.
string describeWidget(const Json &widget, const Json &page)
{
    auto text = [](const Json &j, const char *key) -> string
    {
        return j.contains(key) && j[key].is_string() ? j[key].get<string>() : string();
    };

    string type = text(widget, "type");
    string desc = "widget" + (type.empty() ? string() : " '" + type + "'");

    string io_id = text(widget, "io_id");
    if (!io_id.empty())
        desc += " on io " + io_id;

    string page_name = text(page, "name");
    if (page_name.empty())
        page_name = text(page, "id");
    if (!page_name.empty())
        desc += " of page '" + page_name + "'";

    return desc;
}

}

REGISTER_IO(RemoteUI)

RemoteUI::RemoteUI(Params &p):
    IOBase(p, IOBase::IO_OUTPUT),
    is_online(false),
    is_provisioned(false)
{
    ioDoc->friendlyNameSet("RemoteUI");
    ioDoc->descriptionBaseSet(_("Remote UI device. Represents a remote user interface device. Some actions are available to control the device from rules."));

    Params io_devtype = {{ "waveshare-86-panel", _("Waveshare ESP32-P4-86-Panel") },
                         { "luckfox-86-panel", _("Luckfox Luckfox-Pico-86-Panel") },
                         { "custom", _("Custom device (be sure to configure grid size correctly)") },
                        };

    ioDoc->paramAddList("device_type", _("Device model"), true, io_devtype, "waveshare-86-panel");
    ioDoc->paramAdd("provisioning_code", _("Provisioning code for first time setup"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("auth_token", _("Authentication token"), IODoc::TYPE_STRING, false, "", true);
    ioDoc->paramAdd("device_manufacturer", _("Device manufacturer"), IODoc::TYPE_STRING, false, "", true);
    ioDoc->paramAdd("device_platform", _("Device platform"), IODoc::TYPE_STRING, false, "", true);
    ioDoc->paramAdd("device_secret", _("Device secret for HMAC validation"), IODoc::TYPE_STRING, false, "", true);
    ioDoc->paramAdd("device_version", _("Device version"), IODoc::TYPE_STRING, false, "", true);
    ioDoc->paramAdd("grid_h", _("Grid horizontal size"), IODoc::TYPE_INT, true, "3");
    ioDoc->paramAdd("grid_w", _("Grid vertical size"), IODoc::TYPE_INT, true, "3");
    ioDoc->paramAdd("mac_address", _("Device MAC address"), IODoc::TYPE_STRING, false, "", true);

    ioDoc->actionAdd("set_brightness X", _("Set screen brightness (0-100)"));
    ioDoc->actionAdd("set_page page_id", _("Navigate to a specific page by id"));
    ioDoc->actionAdd("show_notif message", _("Show a notification on screen"));

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

bool RemoteUI::LoadFromXml(pugi::xml_node node)
{
    if (!IOBase::LoadFromXml(node))
        return false;

    // Load device_info
    pugi::xml_node device_info_elem = node.child("calaos:device_info");

    if (!device_info_elem)
    {
        //T3.15: adopt the orphan left under the room node by the writer this
        //ticket fixes, so the information survives the upgrade instead of
        //being dropped at the first save.
        device_info_elem = legacyRoomDeviceInfo(node);
        if (device_info_elem)
            cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): migrating a <calaos:device_info> "
                             "found under the room node, it will be saved inside <calaos:remote_ui>";
    }

    if (device_info_elem)
    {
        device_info = Json::object();

        for (pugi::xml_attribute attr: device_info_elem.attributes())
            device_info[attr.name()] = string(attr.value());
    }

    // Load pages
    pugi::xml_node pages_elem = node.child("calaos:pages");
    if (pages_elem)
    {
        pages = Json::array();

        vector<string> dropped_widgets;
        size_t page_count = 0;
        for (pugi::xml_node page_elem = pages_elem.child("calaos:page");
             page_elem;
             page_elem = page_elem.next_sibling("calaos:page"))
        {
            // Check max pages limit
            if (page_count >= RemoteUISecurityLimits::MAX_PAGES_PER_REMOTEUI)
            {
                cErrorDom(TAG) << "RemoteUI(" << get_param("id") << "): Too many pages ("
                              << page_count << "), maximum is "
                              << RemoteUISecurityLimits::MAX_PAGES_PER_REMOTEUI;
                return false;
            }
            page_count++;

            Json page = Json::object();

            for (pugi::xml_attribute attr: page_elem.attributes())
                page[attr.name()] = string(attr.value());

            // Load widgets for this page
            Json widgets = Json::array();
            size_t widget_count = 0;
            for (pugi::xml_node widget_elem = page_elem.child("calaos:widget");
                 widget_elem;
                 widget_elem = widget_elem.next_sibling("calaos:widget"))
            {
                // Check max widgets per page limit
                if (widget_count >= RemoteUISecurityLimits::MAX_WIDGETS_PER_PAGE)
                {
                    cErrorDom(TAG) << "RemoteUI(" << get_param("id") << "): Too many widgets in page ("
                                  << widget_count << "), maximum is "
                                  << RemoteUISecurityLimits::MAX_WIDGETS_PER_PAGE;
                    return false;
                }
                widget_count++;

                Json widget = Json::object();

                for (pugi::xml_attribute attr: widget_elem.attributes())
                {
                    // Convert numeric attributes
                    string attr_name = attr.name();
                    string attr_value = attr.value();

                    if (attr_name == "x" || attr_name == "y")
                    {
                        //Leaving the attribute OUT hands the widget to the
                        //check below, this loader's existing answer to a widget
                        //without coordinates. Keeping the raw string would
                        //satisfy that check and put a string where the device
                        //expects a number.
                        int coord = 0;
                        if (Utils::from_string_or_keep(attr_value, coord))
                            widget[attr_name] = coord;
                    }
                    else
                        widget[attr_name] = attr_value;
                }

                //Only add widget if it has a type and x/y positions
                if (widget.contains("type") &&
                    widget.contains("x") && widget.contains("y"))
                    widgets.push_back(widget);
                else
                {
                    string what = describeWidget(widget, page);
                    dropped_widgets.push_back(what);
                    cWarningDom(TAG) << "RemoteUI(" << get_param("id")
                                     << "): Ignoring " << what
                                     << ", it needs a type and whole number x/y";
                }
            }

            page["widgets"] = widgets;
            pages.push_back(page);
        }

        //A dropped widget does not come back: the next SaveConfigIO() writes
        //the page without it. Same deferred mail/push channel the rest of the
        //configuration load uses, so a screen cannot lose a button in silence.
        if (!dropped_widgets.empty())
        {
            string report = "Screen '" + get_param("id") + "' lost " +
                            Utils::to_string(dropped_widgets.size()) +
                            " widget(s) whose io.xml declaration is incomplete. They are "
                            "gone from the screen and will be gone from io.xml at the "
                            "next save:";
            for (const string &w: dropped_widgets)
                report += "\n- " + w;

            Config::Instance().reportConfigAlert(report);
        }
    }

    extractReferencedIOs();

    return true;
}

bool RemoteUI::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cnode = node.append_child("calaos:remote_ui");

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        XmlUtils::setAttribute(cnode, key, value);
    }

    //T3.15: the device_info goes INSIDE <calaos:remote_ui>, next to the pages,
    //because that is where LoadFromXml() reads it and what the format
    //documents (RemoteUI/remote-ui.md). It used to be appended to `node` — the
    //ROOM element — which made it a sibling of the device it describes and
    //therefore write-only: nothing ever read it back.
    //Only string values are written, as they always were: the loader turns
    //every attribute into a string, so this is exactly the closure of what can
    //be loaded, and the round trip is lossless.
    if (!device_info.empty())
    {
        pugi::xml_node device_info_elem = cnode.append_child("calaos:device_info");

        for (auto it = device_info.begin(); it != device_info.end(); ++it)
        {
            if (it.value().is_string())
                XmlUtils::setAttribute(device_info_elem, it.key(), it.value().get<string>());
        }
    }

    // Save pages
    if (!pages.empty() && pages.is_array())
    {
        pugi::xml_node pages_elem = cnode.append_child("calaos:pages");

        for (const auto &page : pages)
        {
            pugi::xml_node page_elem = pages_elem.append_child("calaos:page");

            for (auto it = page.begin(); it != page.end(); ++it)
            {
                if (it.key() == "widgets")
                    continue;

                if (it.value().is_string())
                    XmlUtils::setAttribute(page_elem, it.key(), it.value().get<string>());
            }

            // Save widgets
            if (page.contains("widgets") && page["widgets"].is_array())
            {
                for (const auto &widget : page["widgets"])
                {
                    pugi::xml_node widget_elem = page_elem.append_child("calaos:widget");

                    for (auto it = widget.begin(); it != widget.end(); ++it)
                    {
                        if (it.value().is_string())
                            XmlUtils::setAttribute(widget_elem, it.key(), it.value().get<string>());
                        else if (it.value().is_number_integer())
                            XmlUtils::setAttribute(widget_elem, it.key(), std::to_string(it.value().get<int>()));
                    }
                }
            }
        }
    }

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

    //Constant-time comparison of the supplied MAC against the computed raw
    //bytes: the previous std::string operator== short-circuited on the first
    //differing byte, leaking through timing how many leading characters of an
    //attacker-supplied MAC were correct. Length is guarded first, then
    //CRYPTO_memcmp is used over the raw HMAC bytes.
    return HMACAuthenticator::constantTimeHexEquals(hmac, result, result_len);
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

void RemoteUI::putIfSet(Json &data, const string &key, const string &value)
{
    if (!value.empty())
        data[key] = value;
}

Json RemoteUI::getRemoteUIConfigMessage()
{
    Json config;
    putIfSet(config, "name", get_param("name"));
    putIfSet(config, "room", get_param("room"));
    putIfSet(config, "theme", get_param("theme"));
    config["brightness"] = getBrightness();
    config["timeout"] = getTimeout();
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

    RemoteUIManager::Instance().sendCommand(get_param("id"), "remote_ui_set_brightness",
        { { "brightness", brightness } });

    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Brightness set to " << brightness;
    return true;
}

int RemoteUI::getBrightness()
{
    int brightness = 100; // Default value
    //T3.25: _keep. get_param() returns "" for an absent parameter, and a plain
    //from_string() would now turn that into brightness 0 - a screen turned off
    //instead of a screen at full brightness.
    Utils::from_string_or_keep(get_param("brightness"), brightness);
    return brightness;
}

int RemoteUI::getTimeout()
{
    //30 is what every example of the wire format shows (RemoteUI/remote-ui.md);
    //nothing in the tree writes this param, so a screen that was provisioned
    //and never adjusted has no other source for it.
    int timeout = 30;
    Utils::from_string_or_keep(get_param("timeout"), timeout);
    return timeout;
}

bool RemoteUI::setPage(const string &page_id)
{
    RemoteUIManager::Instance().sendCommand(get_param("id"), "remote_ui_set_page",
        { { "page_id", page_id } });

    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Page set to " << page_id;
    return true;
}

bool RemoteUI::showNotification(const string &message)
{
    RemoteUIManager::Instance().sendCommand(get_param("id"), "remote_ui_notification",
        { { "message", message } });

    emitChange();

    cInfoDom(TAG) << "RemoteUI(" << get_param("id") << "): Notification sent: " << message;
    return true;
}

void RemoteUI::emitChange()
{
    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", cmd_state } });
}
