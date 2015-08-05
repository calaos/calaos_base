/******************************************************************************
 **  Copyright (c) 2006-2014, Calaos. All Rights Reserved.
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
#include <WODaliRVB.h>
#include <WagoMap.h>
#include <IOFactory.h>

using namespace Calaos;

REGISTER_OUTPUT(WODaliRVB)
REGISTER_OUTPUT_USERTYPE(WagoOutputDimmerRGB, WODaliRVB)

WODaliRVB::WODaliRVB(Params &_p):
    OutputLightRGB(_p),
    port(502)
{
    // Define IO documentation
    ioDoc->friendlyNameSet("WODaliRVB");
    ioDoc->aliasAdd("WagoOutputDimmerRGB");
    ioDoc->descriptionSet(_("RGB Light using DALI or DMX. To work you need 3 DALI/DMX channels. For DALI you need a 750-641 wago module. For DMX, a DMX4ALL-LAN device connected to the Wago PLC."));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/750-641"));
    ioDoc->linkAdd("Calaos Wiki", _("http://calaos.fr/wiki/fr/dmx-lan"));
    ioDoc->paramAdd("host", _("Wago PLC IP address on the network"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("port", _("Wago ethernet port, default to 502"), IODoc::TYPE_INT, false);

    ioDoc->paramAdd("rline", _("DALI bus line for red channel, usually 1"), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("raddress", _("Device address for red channel. For DALI address is between 1-64. "
                                 "For DMX, the address starts at 100. So for DMX device 5, address should be 105"), IODoc::TYPE_INT, true);
    ioDoc->paramAdd("rgroup", _("Set to 1 if address for red channel is a DALI group address, set to 0 otherwise."), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("rfade_time", _("DALI fade time for red channel. value is between 1-10"), IODoc::TYPE_INT, false);

    ioDoc->paramAdd("gline", _("DALI bus line for green channel, usually 1"), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("gaddress", _("Device address for green channel. For DALI address is between 1-64. "
                                 "For DMX, the address starts at 100. So for DMX device 5, address should be 105"), IODoc::TYPE_INT, true);
    ioDoc->paramAdd("ggroup", _("Set to 1 if address for green channel is a DALI group address, set to 0 otherwise."), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("gfade_time", _("DALI fade time for green channel. value is between 1-10"), IODoc::TYPE_INT, false);

    ioDoc->paramAdd("bline", _("DALI bus line for blue channel, usually 1"), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("baddress", _("Device address for blue channel. For DALI address is between 1-64. "
                                 "For DMX, the address starts at 100. So for DMX device 5, address should be 105"), IODoc::TYPE_INT, true);
    ioDoc->paramAdd("bgroup", _("Set to 1 if address for blue channel is a DALI group address, set to 0 otherwise."), IODoc::TYPE_INT, false);
    ioDoc->paramAdd("bfade_time", _("DALI fade time for blue channel. value is between 1-10"), IODoc::TYPE_INT, false);

    host = get_param("host");
    if (get_params().Exists("port"))
        Utils::from_string(get_param("port"), port);

    WagoMap::Instance(host, port);

    //reqd initial state
    string cmd;
    cmd = "WAGO_DALI_GET " + get_param("rline") + " " + get_param("raddress");
    WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandRed_cb));
    cmd = "WAGO_DALI_GET " + get_param("gline") + " " + get_param("gaddress");
    WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandGreen_cb));
    cmd = "WAGO_DALI_GET " + get_param("bline") + " " + get_param("baddress");
    WagoMap::Instance(host, port).SendUDPCommand(cmd, sigc::mem_fun(*this, &WODaliRVB::WagoUDPCommandBlue_cb));

    Calaos::StartReadRules::Instance().addIO();
    Calaos::StartReadRules::Instance().addIO();
    Calaos::StartReadRules::Instance().addIO();

    cDebugDom("output") << get_param("id") << ": Ok";
}

WODaliRVB::~WODaliRVB()
{
    cDebugDom("output");
}

void WODaliRVB::WagoUDPCommandRed_cb(bool status, string command, string result)
{
    if (!status)
    {
        cInfoDom("output") << "Error with request " << command;
        Calaos::StartReadRules::Instance().ioRead();

        return;
    }

    vector<string> tokens;
    split(result, tokens);
    if (tokens.size() >= 3)
    {
        from_string(tokens[2], red);
        checkReadState();
    }

    Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandGreen_cb(bool status, string command, string result)
{
    if (!status)
    {
        cInfoDom("output") << "Error with request " << command;
        Calaos::StartReadRules::Instance().ioRead();

        return;
    }

    vector<string> tokens;
    split(result, tokens);
    if (tokens.size() >= 3)
    {
        from_string(tokens[2], green);
        checkReadState();
    }

    Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::WagoUDPCommandBlue_cb(bool status, string command, string result)
{
    if (!status)
    {
        cInfoDom("output") << "Error with request " << command;
        Calaos::StartReadRules::Instance().ioRead();

        return;
    }

    vector<string> tokens;
    split(result, tokens);
    if (tokens.size() >= 3)
    {
        from_string(tokens[2], blue);
        checkReadState();
    }

    Calaos::StartReadRules::Instance().ioRead();
}

void WODaliRVB::checkReadState()
{
    if (red < 0 || green < 0 || blue < 0) return;

    ColorValue c(double(red) * 255. / 100.,
                 double(green) * 255. / 100.,
                 double(blue) * 255. / 1000.);

    stateUpdated(c, red != 0 || green != 0 || blue != 0);
}

void WODaliRVB::WagoUDPCommand_cb(bool status, string command, string)
{
    if (!status)
    {
        cInfoDom("output") << "Error with request " << command;

        return;
    }
}

void WODaliRVB::setColorReal(const ColorValue &c, bool s)
{
    int r = 0, g = 0, b = 0;
    if (s)
    {
        r = c.getRed();
        g = c.getGreen();
        b = c.getBlue();
    }

    string cmd = "WAGO_DALI_SET " + get_param("rline") + " " + get_param("rgroup") +
                 " " + get_param("raddress") + " " + Utils::to_string((r * 100) / 255) +
                 " " + get_param("rfade_time");
    WagoMap::Instance(host, port).SendUDPCommand(cmd);

    cmd = "WAGO_DALI_SET " + get_param("gline") + " " + get_param("ggroup") +
          " " + get_param("gaddress") + " " + Utils::to_string((g * 100) / 255) +
          " " + get_param("gfade_time");
    WagoMap::Instance(host, port).SendUDPCommand(cmd);

    cmd = "WAGO_DALI_SET " + get_param("bline") + " " + get_param("bgroup") +
          " " + get_param("baddress") + " " + Utils::to_string((b * 100) / 255) +
          " " + get_param("bfade_time");
    WagoMap::Instance(host, port).SendUDPCommand(cmd);
}

