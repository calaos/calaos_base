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

//T3.2a — the 11 thin KNX IO subclasses, collapsed onto the KNXIo<Base>
//mixin (see KNXIo.h). Each class only states its base type, its ioDoc
//identity, which hooks of the common skeleton it uses, and its actual
//hardware read/write methods. The registered XML type names (REGISTER_IO)
//are unchanged: they are the ABI to existing io.xml configs.

#include "KNXIo.h"
#include "IOFactory.h"

#include "InputAnalog.h"
#include "InputSwitch.h"
#include "InputSwitchLongPress.h"
#include "InputSwitchTriple.h"
#include "InputTemp.h"
#include "OutputAnalog.h"
#include "OutputLight.h"
#include "OutputLightDimmer.h"
#include "OutputLightRGB.h"
#include "OutputShutter.h"
#include "OutputShutterSmart.h"

using namespace Calaos;

namespace Calaos
{

/*****************************************************************************
 * Inputs
 *****************************************************************************/

class KNXInputSwitch: public KNXIo<InputSwitch>
{
public:
    KNXInputSwitch(Params &p):
        KNXIo<InputSwitch>(p, "KNXInputSwitch", _("Input switch with KNX and eibnetmux"))
    {
        readAtStart(KNXValue::EIS_Switch_OnOff);
        onGroupChanged([this](const KNXValue &) { hasChanged(); });
        logKnxGroup();
    }

protected:
    bool readValue() override
    {
        return ctrl()->getValue(knxBase.getReadGroupAddr()).toBool();
    }
};

class KNXInputSwitchLongPress: public KNXIo<InputSwitchLongPress>
{
public:
    KNXInputSwitchLongPress(Params &p):
        KNXIo<InputSwitchLongPress>(p, "KNXInputSwitchLongPress", _("Input switch long press with KNX and eibnetmux"))
    {
        //no read_at_start: reading the current value makes no sense for a
        //long press switch
        onGroupChanged([this](const KNXValue &) { hasChanged(); });
        logKnxGroup();
    }

protected:
    bool readValue() override
    {
        return ctrl()->getValue(knxBase.getReadGroupAddr()).toBool();
    }
};

class KNXInputSwitchTriple: public KNXIo<InputSwitchTriple>
{
public:
    KNXInputSwitchTriple(Params &p):
        KNXIo<InputSwitchTriple>(p, "KNXInputSwitchTriple", _("Input switch triple with KNX and eibnetmux"))
    {
        //no read_at_start: reading the current value makes no sense for a
        //triple click switch
        onGroupChanged([this](const KNXValue &) { hasChanged(); });
        logKnxGroup();
    }

protected:
    bool readValue() override
    {
        return ctrl()->getValue(knxBase.getReadGroupAddr()).toBool();
    }
};

class KNXInputAnalog: public KNXIo<InputAnalog>
{
public:
    KNXInputAnalog(Params &p):
        KNXIo<InputAnalog>(p, "KNXInputAnalog", _("Input analog with KNX and eibnetmux"))
    {
        readAtStart();
        onGroupChanged([this](const KNXValue &) { readValue(); });
        logKnxGroup();
    }

protected:
    void readValue() override { refreshFloatValue(); }
};

class KNXInputTemp: public KNXIo<InputTemp>
{
public:
    KNXInputTemp(Params &p):
        KNXIo<InputTemp>(p, "KNXInputTemp", _("Input temperature with KNX and eibnetmux"))
    {
        readAtStart();
        onGroupChanged([this](const KNXValue &) { readValue(); });
        logKnxGroup();
    }

protected:
    void readValue() override { refreshFloatValue(); }
};

/*****************************************************************************
 * Outputs
 *****************************************************************************/

class KNXOutputAnalog: public KNXIo<OutputAnalog>
{
public:
    KNXOutputAnalog(Params &p):
        KNXIo<OutputAnalog>(p, "KNXOutputAnalog", _("Analog output with KNX and eibnetmux"))
    {
        readAtStart();
        onGroupChanged([this](const KNXValue &v)
        {
            KNXValue val = v;
            val.setEis(configEis());

            value = val.toFloat();
            EmitSignalIO();
            emitChange();
        });
        logKnxGroup();
    }

protected:
    void set_value_real(double val) override
    {
        knxWrite("knx_group", KNXValue::fromFloat(val, configEis()));
    }
};

class KNXOutputLight: public KNXIo<OutputLight>
{
public:
    KNXOutputLight(Params &p):
        KNXIo<OutputLight>(p, "KNXOutputLight", _("Light output with KNX and eibnetmux"))
    {
        useRealState = true;

        readAtStart(KNXValue::EIS_Switch_OnOff);
        onGroupChanged([this](const KNXValue &val)
        {
            if (val.toBool())
                set_value(string("set_state true"));
            else
                set_value(string("set_state false"));
        });
        logKnxGroup();
    }

protected:
    bool set_value_real(bool val) override
    {
        knxWriteSwitch("knx_group", val);
        return true;
    }
};

class KNXOutputLightDimmer: public KNXIo<OutputLightDimmer>
{
public:
    KNXOutputLightDimmer(Params &p):
        KNXIo<OutputLightDimmer>(p, "KNXOutputLightDimmer", _("Light dimmer with KNX and eibnetmux"))
    {
        useRealState = true;

        readAtStart(KNXValue::EIS_Switch_OnOff);
        onGroupChanged([this](const KNXValue &v)
        {
            KNXValue val = v;
            val.setEis(KNXValue::EIS_Dim_UpDown);
            value = val.toInt();
            EmitSignalIO();
            emitChange();
        });
        logKnxGroup();
    }

protected:
    bool set_value_real(int val) override
    {
        knxWrite("knx_group", KNXValue::fromInt(val, KNXValue::EIS_Dim_UpDown));
        return true;
    }
};

class KNXOutputLightRGB: public KNXIo<OutputLightRGB>
{
public:
    KNXOutputLightRGB(Params &p):
        KNXIo<OutputLightRGB>(p, "KNXOutputLightRGB", _("Light RGB with KNX and eibnetmux"),
                              false, //red/green/blue groups replace the common knx_group
                              [](IODoc *doc)
        {
            doc->paramAdd("knx_group_red", _("Red channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
            doc->paramAdd("knx_group_green", _("Green channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
            doc->paramAdd("knx_group_blue", _("Blue channel KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
            doc->paramAdd("listen_knx_group_red", _("Red Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
            doc->paramAdd("listen_knx_group_green", _("Green Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
            doc->paramAdd("listen_knx_group_blue", _("Blue Group address for listening status, Ex: x/y/z"), IODoc::TYPE_STRING, false);
        })
    {
        //TODO (historical, admitted untested — no access to a RGB KNX
        //device): the real state feedback is disabled and nothing listens to
        //the listen_knx_group_* status groups. The pre-refactor code
        //connected a valueChanged handler that filtered the three status
        //groups and then did nothing with the value; that dead connection
        //was dropped here, which is behavior-identical. Implementing the
        //feedback needs testing on real hardware: set useRealState = true
        //and update the color from the received values.
        useRealState = false;

        readAtStart(KNXValue::EIS_Autodetect,
                    { "knx_group_red", "knx_group_green", "knx_group_blue" });
    }

protected:
    void setColorReal(const ColorValue &c, bool s) override
    {
        int r = 0, g = 0, b = 0;
        if (s)
        {
            r = c.getRed();
            g = c.getGreen();
            b = c.getBlue();
        }

        knxWrite("knx_group_red", KNXValue::fromInt(r, KNXValue::EIS_Dim_UpDown));
        knxWrite("knx_group_green", KNXValue::fromInt(g, KNXValue::EIS_Dim_UpDown));
        knxWrite("knx_group_blue", KNXValue::fromInt(b, KNXValue::EIS_Dim_UpDown));
    }
};

class KNXOutputShutter: public KNXIo<OutputShutter>
{
public:
    KNXOutputShutter(Params &p):
        KNXIo<OutputShutter>(p, "KNXOutputShutter", _("Shutter with with KNX and eibnetmux"),
                             false, //up/down groups replace the common knx_group
                             [](IODoc *doc)
        {
            doc->paramAdd("knx_group_up", _("Up KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
            doc->paramAdd("knx_group_down", _("Down KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
        })
    {
    }

protected:
    void setOutputUp(bool enable) override { knxWriteSwitch("knx_group_up", enable); }
    void setOutputDown(bool enable) override { knxWriteSwitch("knx_group_down", enable); }
};

class KNXOutputShutterSmart: public KNXIo<OutputShutterSmart>
{
public:
    KNXOutputShutterSmart(Params &p):
        KNXIo<OutputShutterSmart>(p, "KNXOutputShutterSmart", _("Shutter with with KNX and eibnetmux"),
                                  //historical quirk kept as is: unlike
                                  //KNXOutputShutter, the common
                                  //knx_group/listen_knx_group doc params are
                                  //also documented for this type
                                  true,
                                  [](IODoc *doc)
        {
            doc->paramAdd("knx_group_up", _("Up KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
            doc->paramAdd("knx_group_down", _("Down KNX Group address, Ex: x/y/z"), IODoc::TYPE_STRING, true);
        })
    {
    }

protected:
    void setOutputUp(bool enable) override { knxWriteSwitch("knx_group_up", enable); }
    void setOutputDown(bool enable) override { knxWriteSwitch("knx_group_down", enable); }
};

}

/*****************************************************************************
 * Factory registrations — the NAME here is the XML "type" of existing
 * configurations, do not rename.
 *****************************************************************************/

REGISTER_IO(KNXInputSwitch)
REGISTER_IO(KNXInputSwitchLongPress)
REGISTER_IO(KNXInputSwitchTriple)
REGISTER_IO(KNXInputAnalog)
REGISTER_IO(KNXInputTemp)
REGISTER_IO(KNXOutputAnalog)
REGISTER_IO(KNXOutputLight)
REGISTER_IO(KNXOutputLightDimmer)
REGISTER_IO(KNXOutputLightRGB)
REGISTER_IO(KNXOutputShutter)
REGISTER_IO(KNXOutputShutterSmart)
