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
#include "Device.h"
#include "EventManager.h"
#include "ListeRoom.h"

#include <algorithm>
#include <cmath>

using namespace Calaos;

namespace
{

vector<Device::WriteObserver> &writeObservers()
{
    static vector<Device::WriteObserver> all;
    return all;
}

} //namespace

void Device::observeOutsideWrites(WriteObserver observer)
{
    writeObservers().push_back(observer);
}

void Device::writtenFromOutside(IOBase &io, const char *source)
{
    for (WriteObserver observer: writeObservers())
        observer(io, source);
}

Device::Device(Params &p):
    IOBase(p, IOBase::IO_INOUT)
{
    //Clients pick their widget from gui_type: the family, whatever the impl.
    set_param("gui_type", get_param("type"));

    ioDoc->paramAdd("impl", _("Implementation of the device family: the key of this document in devices."),
                    IODoc::TYPE_STRING, true, string(), true);
}

Device::~Device()
{
    stopListening();

    //Owned IOs never handed to a room: the device was refused at load.
    for (auto &io: loadedIos)
        ListeRoom::Instance().discardUnattachedIO(std::move(io));
}

bool Device::LoadFromXml(pugi::xml_node node)
{
    for (pugi::xml_node child = XmlUtils::firstChildElement(node); child;
         child = XmlUtils::nextSiblingElement(child))
    {
        const string name = child.name();
        if (name == "calaos:link")
        {
            links.push_back({ child.attribute("name").as_string(),
                              child.attribute("io").as_string(),
                              child.attribute("command_io").as_string() });
        }
        else if (name == "calaos:param")
        {
            deviceParams.emplace_back(child.attribute("name").as_string(),
                                      child.attribute("value").as_string());
        }
        else if (name == "calaos:device")
        {
            //Not even built: the cascade does not recurse, and without an
            //impl it would come out as a plain owned IO.
            cErrorDom("device") << "Device(" << get_param("id") << "): devices do not nest, "
                                << "ignoring '" << child.attribute("id").as_string() << "'";
        }
        else if (name == "calaos:input" || name == "calaos:output" ||
                 name == "calaos:internal")
        {
            std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO(child));
            if (!io) continue; //unknown type, already logged by the factory

            if (dynamic_cast<Device *>(io.get()))
            {
                cErrorDom("device") << "Device(" << get_param("id") << "): devices do not nest, "
                                    << "ignoring '" << io->get_param("id") << "'";
                ListeRoom::Instance().discardUnattachedIO(std::move(io));
                continue;
            }

            io->setOwnerDevice(get_param("id"));
            loadedIos.push_back(std::move(io));
        }
    }

    for (const Link &l: links)
        subscribeLink(l.io);

    return true;
}

void Device::subscribeLink(const string &io)
{
    auto changed = [this](IOBase &)
    {
        deviceStateChanged();
    };
    //By id: the subscription may precede an external IO of a later room.
    linkSubscriptions.push_back(IOChangeHub::Instance().subscribe(io, changed));
}

vector<std::unique_ptr<IOBase>> Device::takeLoadedIos()
{
    return std::move(loadedIos);
}

bool Device::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cnode = node.append_child("calaos:device");

    for (int i = 0; i < get_params().size(); i++)
    {
        string key, value;
        get_params().get_item(i, key, value);
        XmlUtils::setAttribute(cnode, key, value);
    }

    for (IOBase *io: ownedIos())
        io->SaveToXml(cnode);

    for (const Link &l: links)
    {
        pugi::xml_node lnode = cnode.append_child("calaos:link");
        XmlUtils::setAttribute(lnode, "name", l.name);
        XmlUtils::setAttribute(lnode, "io", l.io);
        if (!l.command_io.empty())
            XmlUtils::setAttribute(lnode, "command_io", l.command_io);
    }

    for (const auto &p: deviceParams)
    {
        pugi::xml_node pnode = cnode.append_child("calaos:param");
        XmlUtils::setAttribute(pnode, "name", p.first);
        XmlUtils::setAttribute(pnode, "value", p.second);
    }

    return true;
}

vector<IOBase *> Device::ownedIos()
{
    vector<IOBase *> res;
    const string id = get_param("id");

    //Owned IOs are tagged by id: a duplicate device would otherwise claim,
    //and save a second time, the IOs of the device that holds the id.
    ListeRoom &lr = ListeRoom::Instance();
    if (lr.get_io(id) != this)
        return res;

    for (int i = 0; i < lr.size(); i++)
    {
        Room *room = lr.get_room(i);
        for (int j = 0; j < room->get_size(); j++)
        {
            IOBase *io = room->get_io(j);
            if (io->getOwnerDevice() == id)
                res.push_back(io);
        }
    }

    return res;
}

void Device::resolveLinks()
{
    ListeRoom &lr = ListeRoom::Instance();
    for (const Link &l: links)
    {
        if (!lr.get_io(l.io))
            cErrorDom("device") << "Device(" << get_param("id") << "): link '" << l.name
                                << "' targets the unknown IO '" << l.io
                                << "', this capability is unavailable";
        if (!l.command_io.empty() && !lr.get_io(l.command_io))
            cErrorDom("device") << "Device(" << get_param("id") << "): link '" << l.name
                                << "' has the unknown command IO '" << l.command_io
                                << "', its commands are unavailable";
    }

    //A mode restored from the cache is not a change: the device's rules
    //must not fire on the first linked update.
    lastScalar = get_value_string();
}

IOBase *Device::createOwnedLink(const string &role, Params params)
{
    ListeRoom &lr = ListeRoom::Instance();
    const string id = get_param("id");
    //A shadowed duplicate owns nothing, see ownedIos().
    if (lr.get_io(id) != this)
        return nullptr;

    auto link = links.begin();
    while (link != links.end() && link->name != role)
        ++link;
    string ioId = link != links.end() ? link->io : string();
    if (ioId.empty())
    {
        ioId = id + "_" + role;
        if (lr.get_io(ioId) || lr.isIdReserved(ioId))
            ioId = Calaos::get_new_id(ioId + "_");
    }

    params.Add("id", ioId);
    IOBase *io = lr.createIO(params, lr.getRoomByIO(this));
    if (!io)
        return nullptr;
    io->setOwnerDevice(id);

    //A link naming the missing IO is already subscribed to that id.
    if (link == links.end())
    {
        links.push_back({ role, ioId, string() });
        subscribeLink(ioId);
    }
    else if (link->io.empty())
    {
        link->io = ioId;
        subscribeLink(ioId);
    }

    return io;
}

const Device::Link *Device::findLink(const string &name) const
{
    for (const Link &l: links)
    {
        if (l.name == name)
            return &l;
    }
    return nullptr;
}

IOBase *Device::linkIo(const string &name)
{
    const Link *l = findLink(name);
    return l ? ListeRoom::Instance().get_io(l->io) : nullptr;
}

IOBase *Device::linkCommandIo(const string &name)
{
    const Link *l = findLink(name);
    return l ? ListeRoom::Instance().get_io(l->command_io) : nullptr;
}

bool Device::hasDeviceParam(const string &name) const
{
    for (const auto &p: deviceParams)
    {
        if (p.first == name)
            return true;
    }
    return false;
}

string Device::getDeviceParam(const string &name) const
{
    paramReads.insert(name);
    for (const auto &p: deviceParams)
    {
        if (p.first == name)
            return p.second;
    }
    return string();
}

std::set<string> Device::takeParamReads()
{
    return std::move(paramReads);
}

std::string Device::get_value_string()
{
    return scalarFromState(deviceState());
}

std::string Device::scalarFromState(const Json &state)
{
    const auto it = state.find("mode");
    return it != state.end() && it->is_string() ? it->get<string>() : string();
}

//Family field names are printed; anything else is client bytes, and a newline
//in them would forge a log line.
string Device::fieldForLog(const string &name)
{
    bool plain = !name.empty() && name.size() <= 32;
    for (char c: name)
        plain = plain && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
    return plain ? "'" + name + "'" : "#" + Utils::logTag(name);
}

bool Device::set_value(std::string val)
{
    bool applied = false;
    if (!val.empty() && val[0] == '{')
    {
        //Never throws: 1e400 is an out_of_range, not a parse_error, and
        //nothing up to the event loop would catch it.
        const Json command = Json::parse(val, nullptr, false);
        if (command.is_discarded())
            cWarningDom("device") << "Device(" << get_param("id")
                                  << "): command refused, invalid JSON #"
                                  << Utils::logTag(val);
        else
            applied = applyCommand(command);
    }
    else
        applied = applyCommand(Json{ { "mode", val } });

    deviceStateChanged();
    return applied;
}

bool Device::applyCommand(const Json &command)
{
    return applyFields(command);
}

bool Device::applyFields(const Json &command, const vector<string> &handled)
{
    //Json is a sorted map: without this, "fan_mode" would come before "mode".
    auto ours = [&handled](const string &field)
    {
        return std::find(handled.begin(), handled.end(), field) == handled.end();
    };

    vector<string> order;
    if (command.contains("mode") && ours("mode"))
        order.push_back("mode");
    for (const auto &field: command.items())
    {
        if (field.key() != "mode" && ours(field.key()))
            order.push_back(field.key());
    }

    bool applied = false;
    for (const string &field: order)
    {
        string reason;
        if (applyCommandField(field, command.at(field), reason))
            applied = true;
        else
            logRefusedField(field, reason);
    }
    return applied;
}

void Device::logRefusedField(const string &field, const string &reason)
{
    cWarningDom("device") << "Device(" << get_param("id") << "): command field "
                          << fieldForLog(field) << " refused: " << reason;
}

bool Device::applyCommandField(const string &field, const Json &value, string &reason)
{
    VAR_UNUSED(field);
    VAR_UNUSED(value);
    reason = "unknown field for this device";
    return false;
}

bool Device::enumField(const Json &value, const vector<string> &allowed, string &out,
                       string &reason)
{
    if (value.is_string())
    {
        for (const string &a: allowed)
        {
            if (a == value.get<string>())
            {
                out = a;
                return true;
            }
        }
    }

    //The value is not echoed: it is client input, and the list says enough.
    reason = "not one of:";
    for (const string &a: allowed)
        reason += " " + a;
    return false;
}

bool Device::numberField(const Json &value, double min, double max, double step, double &out,
                         string &reason)
{
    if (!value.is_number())
    {
        reason = "not a JSON number";
        return false;
    }

    double v = value.get<double>();
    //A step of 0 means a continuous value.
    if (step > 0)
    {
        v = std::round(v / step) * step;
        //n * step leaves a binary residue (213 * 0.1 is 21.300000000000001),
        //which clients would display: cut at the step's own decimals.
        double scale = 1;
        while (scale < 1e6 && std::fabs(step * scale - std::round(step * scale)) > 1e-9)
            scale *= 10;
        v = std::round(v * scale) / scale;
    }
    out = std::min(std::max(v, min), max);
    return true;
}

Json Device::deviceObject()
{
    return buildDeviceObject(deviceState());
}

Json Device::buildDeviceObject(Json state)
{
    return Json{ { "family", get_param("type") },
                 { "impl", get_param("impl") },
                 { "capabilities", deviceCapabilities() },
                 { "state", std::move(state) },
                 { "ios", deviceIos() } };
}

Json Device::deviceCapabilities()
{
    return Json::object();
}

Json Device::deviceState()
{
    return Json::object();
}

Json Device::deviceIos()
{
    std::map<string, string> ios;
    for (const Link &l: links)
    {
        if (ListeRoom::Instance().get_io(l.io))
            ios[l.name] = l.io;
    }
    return Json(ios);
}

void Device::deviceStateChanged()
{
    const string scalar = get_value_string();
    if (scalar != lastScalar)
    {
        lastScalar = scalar;
        EmitSignalIO();
    }

    if (publishTimer)
        return;

    //Not re-armed by later changes: a steady stream still publishes every
    //PUBLISH_DELAY instead of never.
    auto fire = [this]()
    {
        Device *self = this;
        //A Timer repeats: drop it, which its own slot may do.
        publishTimer.reset();
        self->publish();
    };
    publishTimer = std::make_unique<Timer>(PUBLISH_DELAY, fire);
}

void Device::stopListening()
{
    linkSubscriptions.clear();
    publishTimer.reset();
}

void Device::publish()
{
    Json state = deviceState();
    Params p;
    p.Add("state", scalarFromState(state));
    EventManager::createWithObjects(CalaosEvent::EventIOChanged, get_param("id"), p,
                                    Json{ { "device", buildDeviceObject(std::move(state)) } });
}
