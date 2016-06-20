/******************************************************************************
 **  Copyright (c) 2006-2015, Calaos. All Rights Reserved.
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
#include <jansson.h>

#include "HueOutputLightRGB.h"
#include "IOFactory.h"
#include "UrlDownloader.h"
#include "Jansson_Addition.h"

namespace Calaos {

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

    m_host = get_param("host");
    m_api = get_param("api");
    m_idHue = get_param("id_hue");

    m_timer = new EcoreTimer(2.0,[=](){
        std::string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue;
        UrlDownloader *dl = new UrlDownloader(url, true);
        dl->m_signalCompleteData.connect([&](Eina_Binbuf *downloadedData, int status)
    {
         if (status)
         {
              json_error_t error;
              const unsigned char* c = eina_binbuf_string_get(downloadedData);
              json_t *root = json_loads((const char*)eina_binbuf_string_get(downloadedData), 0, &error);
              if (!root)
              {
                   cErrorDom("hue") << "Json received malformed : " << error.source
                                    << " " << error.text << " (" << Utils::to_string(error.line) << " )";
                   return;
              }
              if (!json_is_object(root))
              {
                   cErrorDom("hue") << "Protocol changed ? date received : " << eina_binbuf_string_get(downloadedData);
                   return;
              }

              json_t *tstate = json_object_get(root, "state");
              if (!tstate || !json_is_object(tstate))
              {
                   cErrorDom("hue") << "Protocol changed ? date received : " << eina_binbuf_string_get(downloadedData);
                   return;
              }

              int sat, bri, hue;
              bool on, reachable;

              sat = json_integer_value(json_object_get(tstate, "sat"));
              bri = json_integer_value(json_object_get(tstate, "bri"));
              hue = json_integer_value(json_object_get(tstate, "hue"));
              on = jansson_bool_get(tstate, "on");
              reachable = jansson_bool_get(tstate, "reachable");

              cDebugDom("hue") << "State: " << on << " Hue : " << hue << " Bri: " << bri << " Hue : " << hue << "Data : " << c;

              if (reachable)
                   stateUpdated(ColorValue::fromHsl((int)(hue * 360.0 / 65535.0),
                                                    (int)(sat * 100.0 / 255.0),
                                                    (int)(bri * 100.0 / 255.0)), on);
              else
                   stateUpdated(ColorValue(0,0,0), reachable);

              json_decref(root);
         }
         else
         {
              stateUpdated(ColorValue(0,0,0), false);
         }
    });

        if (!dl->httpGet())
             delete dl;
         });
}

HueOutputLightRGB::~HueOutputLightRGB()
{
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

void HueOutputLightRGB::setOff()
{
    std::string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue + "/state";
    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->bodyDataSet("{\"on\":false}");
    dl->m_signalCompleteData.connect([&](Eina_Binbuf *downloadedData, int status)
    {
        cDebugDom("hue") << "datareceived: " << eina_binbuf_string_get(downloadedData);
    });

    dl->httpPut();
}


void HueOutputLightRGB::setColor(const ColorValue &c)
{
    std::string url = "http://" + m_host + "/api/" + m_api + "/lights/" + m_idHue + "/state";
    UrlDownloader *dl = new UrlDownloader(url, true);
    std::string ccolor = "{\"on\":true,"
                   "\"sat\":"  + Utils::to_string((int)(c.getHSVSaturation() * 255.0 / 100.0)) +
                   ",\"bri\":" + Utils::to_string((int)(c.getHSLLightness() * 255.0 / 100.0)) +
                   ",\"hue\":" + Utils::to_string((int)(c.getHSLHue() * 65535.0 / 360.0)) + "}";
    dl->bodyDataSet(ccolor);
    dl->m_signalCompleteData.connect([&](Eina_Binbuf *downloadedData, int status)
    {
        VAR_UNUSED(status);
        cDebugDom("hue") << "datareceived: " << eina_binbuf_string_get(downloadedData);
    });

    dl->httpPut();
}

}
