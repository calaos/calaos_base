/******************************************************************************
*  Copyright (c) 2007-2008, Calaos. All Rights Reserved.
**
**  This file is part of Calaos Home.
**
**  Calaos Home is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 3 of the License, or
**  (at your option) any later version.
**
**  Calaos Home is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with Foobar; if not, write to the Free Software
**  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
**
******************************************************************************/
#include <IOFactory.h>
#include <WagoMap.h>

#include <InputTime.h>
#include <InputTimer.h>
#include <OutputFake.h>
#include <WODigital.h>
#include <WIDigitalBP.h>
#include <WIDigitalTriple.h>
#include <WIDigitalLong.h>
#include <Squeezebox.h>
#include <IntValue.h>
#include <WITemp.h>
#include <Gadspot.h>
#include <Axis.h>
#include <StandardMjpeg.h>
#include <Planet.h>
#include <Scenario.h>
#include <WOVolet.h>
#include <WOVoletSmart.h>
#include <X10Output.h>
#include <InPlageHoraire.h>
#include <WODali.h>
#include <WODaliRVB.h>
#include <WIAnalog.h>
#include <WOAnalog.h>
#include <OWTemp.h>
#include <AVReceiver.h>
#include <WebAnalogIn.h>
#include <GpioOutputSwitch.h>
#include <ZibaseAnalogIn.h>
#include <ZibaseDigitalIn.h>

using namespace Calaos;

void IOFactory::readParams(TiXmlElement *node, Params &p)
{
    TiXmlAttribute *attr = node->ToElement()->FirstAttribute();

    for (; attr; attr = attr->Next())
    {
        p.Add(attr->Name(), attr->ValueStr());
    }
}

Input *IOFactory::CreateInput(std::string type, Params &params)
{
    Input *in = NULL;

    if (type == "InputTime")
    {
        in = new InputTime(params);
    }
    else if (type == "InputTimer")
    {
        in = new InputTimer(params);
    }
    else if (type == "WIDigitalBP" || type == "WIDigital")
    {
        in = new WIDigitalBP(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WIDigitalTriple")
    {
        in = new WIDigitalTriple(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WIDigitalLong")
    {
        in = new WIDigitalLong(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WITemp")
    {
        in = new WITemp(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WIAnalog")
    {
        in = new WIAnalog(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "OWTemp")
    {
        in = new OWTemp(params);
    }
    else if (type == "WebAnalogIn")
    {
        in = new WebAnalogIn(params);
    }
    else if (type == "scenario")
    {
        in = new Scenario(params);
    }
    else if (type == "InPlageHoraire")
    {
        in = new InPlageHoraire(params);
    }
    else if (type == "internbool" || type == "internalbool" ||
             type == "InternalBoolOutput" || type == "InternalBoolInput" ||
             type == "InternalBool")
    {
        type = "InternalBool";
        params.Add("type", type);

        in = new Internal(params);
    }
    else if (type == "internint" || type == "internalint" ||
             type == "InternalIntOutput" || type == "InternalIntInput" ||
             type == "InternalInt")
    {
        type = "InternalInt";
        params.Add("type", type);

        in = new Internal(params);
    }
    else if (type == "internstring" || type == "internalstring" ||
             type == "InternalStringOutput" || type == "InternalStringInput" ||
             type == "InternalString")
    {
        type = "InternalString";
        params.Add("type", type);

        in = new Internal(params);
    }
    else if (type == "ZibaseAnalogIn")
    {
        in = new ZibaseAnalogIn(params);
    }
    else if (type == "ZibaseDigitalIn")
    {
        in = new ZibaseDigitalIn(params);
    }

    if (in)
        cInfo() <<  "IOFactory::CreateInput(" << type << "): Ok";
    else
        cWarning() <<  "IOFactory::CreateInput(" << type << "): Unknown Input type !";

    return in;
}

Input *IOFactory::CreateInput(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    Input *in = CreateInput(p["type"], p);
    in->LoadFromXml(node);

    return in;
}

Output *IOFactory::CreateOutput(std::string type, Params &params)
{
    Output *out = NULL;

    if (type == "OutputFake")
    {
        out = new OutputFake(params);
    }
    else if (type == "WODigital")
    {
        out = new WODigital(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WONeon")
    {
        cError() <<  "IOFactory::CreateOutput(" << type << "): WONeon is deprecated !";
    }
    else if (type == "X10Output")
    {
        out = new X10Output(params);
    }
    else if (type == "WOVolet")
    {
        out = new WOVolet(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WOVoletSmart")
    {
        out = new WOVoletSmart(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "WODali")
    {
        out = new WODali(params);
    }
    else if (type == "WODaliRVB")
    {
        out = new WODaliRVB(params);
    }
    else if (type == "WOAnalog")
    {
        out = new WOAnalog(params);
        int port;
        Utils::from_string(params["port"], port);
        std::string host = params["host"];
        if (!params.Exists("port"))
            port = 502;

        WagoMap::Instance(host, port);
    }
    else if (type == "AVReceiver")
    {
        out = new IOAVReceiver(params);
    }
    else if (type == "GpioOutputSwitch")
    {
        out = new GpioOutputSwitch(params);
    }

    if (out)
        cInfo() <<  "IOFactory::CreateOutput(" << type << "): Ok";
    else
        cWarning() <<  "IOFactory::CreateOutput(" << type << "): Unknown Output type !";

    return out;
}

Output *IOFactory::CreateOutput(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    Output *out = CreateOutput(p["type"], p);
    out->LoadFromXml(node);

    return out;
}

AudioPlayer *IOFactory::CreateAudio (std::string type, Params &params)
{
    AudioPlayer *player = NULL;

    if (type == "slim")
    {
        player = new Squeezebox(params);
    }

    if (player)
        cInfo() <<  "IOFactory::CreateAudio(" << type << "): Ok";
    else
        cWarning() <<  "IOFactory::CreateAudio(" << type << "): Unknown Input type !";

    return player;
}

AudioPlayer *IOFactory::CreateAudio(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    AudioPlayer *pl = CreateAudio(p["type"], p);
    pl->LoadFromXml(node);

    return pl;
}

IPCam *IOFactory::CreateIPCamera (std::string type, Params &params)
{
    IPCam *cam = NULL;

    if (type == "axis")
    {
        cam = new Axis(params);
    }
    else if (type == "gadspot")
    {
        cam = new Gadspot(params);
    }
    else if (type == "planet")
    {
        cam = new Planet(params);
    }
    else if (type == "standard_mjpeg")
    {
        cam = new StandardMjpeg(params);
    }

    if (cam)
        cInfo() <<  "IOFactory::CreateIPCamera(" << type << "): Ok";
    else
        cWarning() <<  "IOFactory::CreateIPCamera(" << type << "): Unknown Input type !";

    return cam;
}

IPCam *IOFactory::CreateIPCamera(TiXmlElement *node)
{
    Params p;
    readParams(node, p);

    IPCam *cam = CreateIPCamera(p["type"], p);
    cam->LoadFromXml(node);

    return cam;
}
