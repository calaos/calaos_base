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
#include "IOChangeHub.h"
#include "IOFactory.h"
#include "Timer.h"

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
    ~Device();

    DATA_TYPE get_type() override { return TSTRING; }

    /* The scalar rules see. By default the "mode" of deviceState(), empty
     * when the device has none. */
    std::string get_value_string() override;

    /* One command path for rules, Lua and set_state: a JSON object (the
     * string starts with '{') is a partial update, anything else is a plain
     * mode ("off" is {"mode":"off"}). Fields are applied one by one through
     * applyCommandField(), best effort. True when at least one field was
     * applied. */
    using IOBase::set_value;
    bool set_value(std::string val) override;

    /* The "device" object of get_home and io_changed. Always sent whole:
     * clients replace their copy, so nothing may be left out to mean
     * "unchanged". */
    Json deviceObject();

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

    //Coalescing window of the io_changed published for the device.
    static constexpr double PUBLISH_DELAY = 0.1;

protected:
    /* Applies one command field, or returns false with `reason` set. Only a
     * field this instance supports may be applied (a capability exists iff
     * its link does). The base knows no field. */
    virtual bool applyCommandField(const string &field, const Json &value, string &reason);

    //The three members of deviceObject() a family fills in.
    virtual Json deviceCapabilities();
    virtual Json deviceState();
    //Role -> IO id, for the links whose IO exists.
    virtual Json deviceIos();

    /* To be called when something deviceObject() shows has changed: emits
     * the device's own signal for the rules when its scalar moved, and
     * schedules one io_changed. Linked IO changes already call it. */
    void deviceStateChanged();

    //Validation helpers for applyCommandField().
    static bool enumField(const Json &value, const vector<string> &allowed, string &out,
                          string &reason);
    //Rounded to a multiple of step (none when 0), then clamped to [min, max].
    static bool numberField(const Json &value, double min, double max, double step, double &out,
                            string &reason);

private:
    const Link *findLink(const string &name) const;
    void publish();

    vector<Link> links;
    //A vector and not Params: the file order is kept on save.
    vector<std::pair<string, string>> deviceParams;
    vector<std::unique_ptr<IOBase>> loadedIos;

    vector<IOChangeHub::Subscription> linkSubscriptions;
    std::unique_ptr<Timer> publishTimer;
    string lastScalar;
};

} //namespace Calaos

#define REGISTER_DEVICE(IMPL, TYPE)                                                             \
    static Registrar IMPL##_reg_device_##TYPE(string(Calaos::IOFactory::DEVICE_PREFIX) + #IMPL, \
                                              function<IOBase *(Params &)>(                     \
                                                  [](Params &_p) -> IOBase * {                  \
                                                      return new TYPE(_p);                      \
                                                  }));

#endif
