/******************************************************************************
 **  Copyright (c) 2006-2024, Calaos. All Rights Reserved.
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
#include "AVRRose.h"
#include "UrlDownloader.h"

using namespace Calaos;

const int HIFIROSE_DEFAULT_PORT = 9283;

/*
HifiRose devices are using a simple REST API without any
authentication or security.
*/

AVRRose::AVRRose(Params &p):
    AVReceiver(p, HIFIROSE_DEFAULT_PORT, AVR_CON_CUSTOM)
{
    cInfoDom("hifirose") << params["host"];
}

AVRRose::~AVRRose()
{
}

void AVRRose::Power(bool on, int zone)
{
    pollStatus([on, this]()
    {
        if ((on && power_main) ||
            (!on && !power_main))
            return;

        Json d = {
            { "barControl", "remote_bar_order_sleep_on_off" },
            { "value", "-1" }
        };

        postRequest("remote_bar_order", d.dump(), [this](const string &)
        {
            pollStatus();
        });
    });
}

void AVRRose::setVolume(int volume, int zone)
{
    Json d = {
        { "volumeType", "volume_set" },
        { "volumeValue", Utils::to_string(volume) }
    };

    postRequest("volume", d.dump(), [this](const string &)
    {
        pollStatus();
    });
}

void AVRRose::selectInputSource(int source, int zone)
{
}

void AVRRose::sendCustomCommand(string command)
{
}

void AVRRose::pollStatus(std::function<void()> nextCb)
{
    postRequest("get_current_state", {}, [this, nextCb](const string &data)
    {
        Json jdoc;
        try
        {
            jdoc = Json::parse(data);
            if (!jdoc.is_object())
            {
                cWarningDom("hifirose") << "Invalid JSON data";
                return;
            }

            string code = jdoc["code"];

            if (code == "SLEEP")
            {
                power_main = false;
                state_changed_1.emit("power", "false");
            }
            else if (code == "G0000")
            {
                power_main = true;
                state_changed_1.emit("power", "true");
            }
            else
            {
                cWarningDom("hifirose") << "Unknown status code: " << jdoc["code"];
            }
        }
        catch(const std::exception& e)
        {
            cWarningDom("hifirose") << "Error parsing '" << data << "':" << e.what();
        }

        //Get current volume
        Json d = {
            { "volumeType", "volume_get" }
        };

        postRequest("volume", d.dump(), [this, nextCb](const string &dataRes)
        {
            Json jdocvol;
            try
            {
                jdocvol = Json::parse(dataRes);
                if (!jdocvol.is_object())
                {
                    cWarningDom("hifirose") << "Invalid JSON data";
                    return;
                }

                volume_main = jdocvol["volumeValue"];
                state_changed_1.emit("volume", Utils::to_string(volume_main));
            }
            catch(const std::exception& e)
            {
                cWarningDom("hifirose") << "Error parsing '" << dataRes << "':" << e.what();
            }

            if (nextCb)
                nextCb();
        });
    });
}

void AVRRose::postRequest(string urlPath, string data, std::function<void(const string &)> dataCb)
{
    string url = "http://" + host + ":" + Utils::to_string(port) + "/" + urlPath;

    cDebugDom("hifirose") << "POST " << url << " data: " << data;

    UrlDownloader *dl = new UrlDownloader(url, true);
    dl->setHeader("Content-Type", "application/json");
    dl->httpPost({}, data);

    dl->m_signalCompleteData.connect([dataCb](const string &dataRes, int status)
    {
        if (status != 200)
        {
            cErrorDom("hifirose") << "Error while posting data to HifiRose device: " << status;
            return;
        }

        if (dataCb)
            dataCb(dataRes);
    });
}
