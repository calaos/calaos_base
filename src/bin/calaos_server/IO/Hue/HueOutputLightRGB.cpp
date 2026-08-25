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
#include "HueOutputLightRGB.h"
#include "HueWire.h"
#include "IOFactory.h"
#include "UrlDownloader.h"

using namespace Calaos;

REGISTER_IO(HueOutputLightRGB)

HueOutputLightRGB::HueOutputLightRGB(Params &p):
    OutputLightRGB(p)
{
    ioDoc->friendlyNameSet("HueOutputLightRGB");
    ioDoc->descriptionSet(_("RGB Light dimmer using a Philips Hue"));
    ioDoc->linkAdd("Meet Hue", _("http://www.meethue.com"));
    ioDoc->paramAdd("host", _("Hue bridge IP address"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("api", _("API key return by Hue bridge when assciation has been made. Use Hue Wizard in calaos_installer to get this value automatically."), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("id_hue", _("Unique ID describing the Hue Light. This value is returned by the Hue Wizard."), IODoc::TYPE_STRING, true);
    //T2.19: insecure by default (user decision) — the Hue bridge serves a
    //self-signed certificate, existing configs have no insecure param
    ioDoc->paramAdd("insecure", _("Skip TLS certificate verification when connecting to the Hue bridge. Default to true (the bridge uses a self-signed certificate). Set to false to only allow a verified HTTPS connection."), IODoc::TYPE_BOOL, false, "true");

    m_host = get_param("host");
    m_api = get_param("api");
    m_idHue = get_param("id_hue");

    m_timer = new Timer(2.0, [=]()
    {
        string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue;
        UrlDownloader *dl = new UrlDownloader(url, true);
        dl->setInsecureFromParam(get_param("insecure")); //T2.19: insecure unless insecure="false"
        dl->m_signalCompleteData.connect([&](const string &downloadedData, int status)
        {
            if (status)
            {
                //E4.1d: the whole reader lives in HueWire.h so that
                //tests/HueWire_test.cpp can call the SHIPPED code. Its integer
                //and boolean reads reproduce the jansson defaults by hand: the
                //"obvious" j.value("sat", 0) THROWS on a string or a null, and
                //a throw here is a throw in a download callback with no
                //try/catch on the path.
                HueWire::LightState st;
                const HueWire::Decode decoded = HueWire::decodeLightState(downloadedData, st);

                if (decoded == HueWire::Decode::Malformed)
                {
                    //jansson gave an error.source/text/line here; nlohmann's
                    //non-throwing parse has no message to give, so the answer
                    //itself is logged instead - the same thing the two
                    //"Protocol changed ?" lines below already log.
                    cErrorDom("hue") << "Json received malformed : " << downloadedData;
                    return;
                }
                if (decoded != HueWire::Decode::Ok)
                {
                    //NotAnObject and NoState logged the same line before and
                    //still do
                    cErrorDom("hue") << "Protocol changed ? date received : " << downloadedData;
                    return;
                }

                cDebugDom("hue") << "State: " << st.on << " Hue : " << st.hue << " Bri: " << st.bri << " Hue : " << st.hue << "Data : " << downloadedData;

                const HueWire::StateUpdate update = HueWire::toStateUpdate(st);
                updateHueState(update.color, update.on);
            }
            else
            {
                updateHueState(ColorValue(), false);
            }
        });

        if (!dl->httpGet())
            delete dl;
    });
}

HueOutputLightRGB::~HueOutputLightRGB()
{
    //Stops and destroys the polling timer. Its lambda captures this, it must
    //not be able to fire once this object is gone.
    DELETE_NULL(m_timer);
}

void HueOutputLightRGB::setColorReal(const ColorValue &c, bool s)
{
    if (!s)
    {
        cDebugDom("hue") << "State OFF ";
        setOff();
    }
    else
    {
        cDebugDom("hue") << "Hue color: " << c.toString();
        setColor(c);
    }
}

void HueOutputLightRGB::updateHueState(const ColorValue &c, bool s)
{
    if (c != lastColor || s != lastState)
    {
        lastColor = c;
        lastState = s;
        stateUpdated(c, s);
    }
}

void HueOutputLightRGB::setOff()
{
    string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue + "/state";
    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->setInsecureFromParam(get_param("insecure")); //T2.19: insecure unless insecure="false"
    dl->bodyDataSet("{\"on\":false}");
    dl->m_signalCompleteData.connect([&](const string &downloadedData, int status)
    {
        cDebugDom("hue") << "datareceived: " << downloadedData;
    });

    dl->httpPut();
}


void HueOutputLightRGB::setColor(const ColorValue &c)
{
    string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue + "/state";
    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->setInsecureFromParam(get_param("insecure")); //T2.19: insecure unless insecure="false"
    string ccolor = "{\"on\":true,"
                   "\"sat\":"  + Utils::to_string((int)(c.getHSVSaturation() * 255.0 / 100.0)) +
                   ",\"bri\":" + Utils::to_string((int)(c.getHSLLightness() * 255.0 / 100.0)) +
                   ",\"hue\":" + Utils::to_string((int)(c.getHSLHue() * 65535.0 / 360.0)) + "}";
    dl->bodyDataSet(ccolor);
    dl->m_signalCompleteData.connect([&](const string &downloadedData, int status)
    {
        VAR_UNUSED(status);
        cDebugDom("hue") << "datareceived: " << downloadedData;
    });

    dl->httpPut();
}
