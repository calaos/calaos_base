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
#include "ListeRoom.h"

using namespace Calaos;

Device::Device(Params &p):
    IOBase(p, IOBase::IO_INOUT)
{
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
        else if (name == "calaos:input" || name == "calaos:output" ||
                 name == "calaos:internal" || name == "calaos:device")
        {
            std::unique_ptr<IOBase> io(IOFactory::Instance().CreateIO(child));
            if (!io) continue; //unknown type, already logged by the factory

            //Owned IOs are deleted by their device's cascade, which does not
            //recurse: a nested device would leak its own IOs into the room.
            if (dynamic_cast<Device *>(io.get()))
            {
                cErrorDom("device") << "Device(" << get_param("id") << "): devices do not nest, "
                                    << "ignoring '" << io->get_param("id") << "'";
                continue;
            }

            io->setOwnerDevice(get_param("id"));
            loadedIos.push_back(std::move(io));
        }
    }

    return true;
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
    for (const auto &p: deviceParams)
    {
        if (p.first == name)
            return p.second;
    }
    return string();
}
