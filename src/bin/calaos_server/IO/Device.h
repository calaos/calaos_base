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
#ifndef S_DEVICE_H
#define S_DEVICE_H

#include <memory>
#include <vector>

#include "IOBase.h"
#include "IOFactory.h"

namespace Calaos
{

/* An IO declared by <calaos:device> in a room: owned IOs nested in its
 * element, links giving a role to an owned or external IO, and params.
 * The room owns every IO, owned ones included; the device only knows ids. */
class Device: public IOBase
{
public:
    struct Link
    {
        string name;
        string io;
        string command_io;
    };

    Device(Params &p);

    DATA_TYPE get_type() override { return TSTRING; }

    bool LoadFromXml(pugi::xml_node node) override;
    bool SaveToXml(pugi::xml_node node) override;

    /* The owned IOs built by LoadFromXml(), which the room takes over. They
     * cannot be added to the room from here: the device is not in it yet. */
    vector<std::unique_ptr<IOBase>> takeLoadedIos();

    /* Called once every room is loaded, since a link may target an IO of
     * another room. Logs the unresolved ones; a subclass overrides it to
     * subscribe to its links and must call the base. */
    virtual void resolveLinks();

    /* Non-owning, valid until the next IO deletion. Empty for a device
     * shadowed by another one with the same id: the first registered one
     * owns the IOs, as it owns the id. */
    vector<IOBase *> ownedIos();

    const vector<Link> &getLinks() const { return links; }

    //Resolved at each call; nullptr when the link or its IO is missing.
    IOBase *linkIo(const string &name);
    IOBase *linkCommandIo(const string &name);

    bool hasDeviceParam(const string &name) const;
    string getDeviceParam(const string &name) const;

private:
    const Link *findLink(const string &name) const;

    vector<Link> links;
    //A vector and not Params: the file order is kept on save.
    vector<std::pair<string, string>> deviceParams;
    vector<std::unique_ptr<IOBase>> loadedIos;
};

} //namespace Calaos

#define REGISTER_DEVICE(IMPL, TYPE)                                                             \
    static Registrar IMPL##_reg_device_##TYPE(string(Calaos::IOFactory::DEVICE_PREFIX) + #IMPL, \
                                              function<IOBase *(Params &)>(                     \
                                                  [](Params &_p) -> IOBase * {                  \
                                                      return new TYPE(_p);                      \
                                                  }));

#endif
