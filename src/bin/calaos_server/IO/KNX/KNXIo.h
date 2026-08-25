/******************************************************************************
 **  Copyright (c) 2006-2026, Calaos. All Rights Reserved.
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
#ifndef KNXIO_H
#define KNXIO_H

#include "ThinIo.h"
#include "KNXBase.h"
#include "AnalogIO.h"

namespace Calaos
{

//KNX mixin for the thin KNX IO subclasses (T3.2a).
//
//All KNXInput*/KNXOutput* classes used to repeat the same skeleton:
//ioDoc setup, a heap-allocated KNXBase, an optional read-at-start
//Timer::singleShot, a KNXCtrl::valueChanged connection filtered on the
//listen group address, and an info log. This template owns that skeleton;
//the concrete classes (KNXIo.cpp) only declare what differs.
template <typename Base>
class KNXIo: public ThinIo<Base>
{
protected:
    KNXIo(Params &p, const char *friendlyName, const string &description,
          bool add_doc_group = true,
          const std::function<void(IODoc *)> &extraDoc = {}):
        ThinIo<Base>(p, friendlyName, description, extraDoc),
        knxBase(&this->param, this->ioDoc, add_doc_group)
    {
    }

    KNXBase knxBase;

    shared_ptr<KNXCtrl> ctrl()
    {
        return KNXCtrl::Instance(this->get_param("host"));
    }

    //eis type from the config, EIS_Autodetect when absent/invalid
    int configEis()
    {
        int eis = KNXValue::EIS_Autodetect;
        Utils::from_string(this->get_param("eis"), eis);
        return eis;
    }

    //When read_at_start is set, send a read request shortly after startup to
    //get the current value. Reads the common knx_group by default, or each
    //of the given group address parameters (multi-group IOs like RGB).
    void readAtStart(int eis = KNXValue::EIS_Autodetect,
                     const vector<string> &group_bases = {})
    {
        if (this->get_param("read_at_start") != "true")
            return;

        //T3.40: 1.5 s is the widest window of the whole tree, and it opens at
        //construction for every one of the eleven KNX IO types. Armed through
        //the IO's lifetime tag (IOBase::ioAlive): both ctrl() and knxBase
        //dereference `this`, so an IO deleted meanwhile used to crash here.
        this->ioAlive.singleShot(1.5, [this, eis, group_bases]()
        {
            if (group_bases.empty())
                ctrl()->readValue(knxBase.getReadGroupAddr(), eis);
            else
                for (const string &g: group_bases)
                    ctrl()->readValue(knxBase.getReadGroupAddr(g), eis);
        });
    }

    //Forward KNXCtrl::valueChanged to cb for events on our listen group
    void onGroupChanged(const std::function<void(const KNXValue &)> &cb)
    {
        ctrl()->valueChanged.connect([this, cb](const string group_addr, const KNXValue &v)
        {
            if (group_addr == knxBase.getReadGroupAddr())
                cb(v);
        });
    }

    //Historically logged in the "input" domain by outputs too, kept as is
    void logKnxGroup()
    {
        cInfoDom("input") << "knx_group: " << knxBase.getReadGroupAddr();
    }

    //Cached value of our listen group, with the configured eis applied
    KNXValue cachedValue()
    {
        KNXValue val = ctrl()->getValue(knxBase.getReadGroupAddr());
        val.setEis(configEis());
        return val;
    }

    void knxWrite(const string &group_param, const KNXValue &kval)
    {
        ctrl()->writeValue(this->get_param(group_param), kval);
    }

    void knxWriteSwitch(const string &group_param, bool enable)
    {
        knxWrite(group_param, KNXValue::fromInt(enable ? 1 : 0, KNXValue::EIS_Switch_OnOff));
    }

    //Shared readValue() body of the analog-like inputs (Analog/Temp)
    void refreshFloatValue()
    {
        KNXValue val = cachedValue();

        if (this->value != val.toFloat())
        {
            this->value = AnalogIO::convertValue(this->get_params(), val.toFloat());
            this->emitChange();
        }
    }
};

}

#endif // KNXIO_H
