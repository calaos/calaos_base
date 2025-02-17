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
#include "SynoSurveillanceStation.h"
#include "IOFactory.h"
#include "UrlDownloader.h"

using namespace Calaos;

REGISTER_IO(SynoSurveillanceStation)

const string SynoApi = "%1webapi/%2?api=%3&method=%4&version=%5";
const string SynoApiInfo = "%1webapi/query.cgi?api=SYNO.API.Info&method=Query&version=1&query=%2";

SynoSurveillanceStation::SynoSurveillanceStation(Params &p):
    IPCam(p)
{
    ioDoc->descriptionBaseSet(_("Synology Surveillance Station IP Camera. Camera can be viewed directly inside calaos and used in rules."));
    ioDoc->paramAdd("url", _("Full url to Synology nas. Ex: https://192.168.0.22:5000"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("username", _("Username which can access Surveillance Station"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("password", _("Password for user"), IODoc::TYPE_STRING, true);
    ioDoc->paramAdd("camera_id", _("ID of the camera"), IODoc::TYPE_STRING, true);
    ioDoc->paramAddInt("camera_profile", _("Profile to use for snapshot. 0- High quality, 1- Balanced, 2- Low bandwidth"), 0, 2, false, 1);

    if (!Utils::strStartsWith(get_param("url"), "http"))
        set_param("url", string("http://") + get_param("url"));
    if (get_param("url")[get_param("url").length() - 1] != '/')
        set_param("url", get_param("url") + '/');
}

SynoSurveillanceStation::~SynoSurveillanceStation()
{
    //Logout here?
}

void SynoSurveillanceStation::downloadSnapshot(std::function<void(const string &)> dataCb)
{
    cDebugDom("syno.ss") << "downloadSnapshot()";

    if (isRunning)
    {
        cDebugDom("syno.ss") << "already running, returning last screenshot.";
        dataCb(lastSnapshot);
        return;
    }

    isRunning = true;
    downloadDataCb = dataCb;

    if (authUrl.empty())
    {
        cDebugDom("syno.ss") << "Probing API info for Login";
        apiSid.clear();
        snapUrl.clear();
        getApiInfo("SYNO.API.Auth", "Login", "6", [=](const string &api)
        {
            if (api.empty())
            {
                cWarning() << "Syno: Fail to get api info for SYNO.API.Auth";
                isRunning = false;
                downloadDataCb({});
                return;
            }

            authUrl = api;
            tryLogin();
        });
    }
    else
    {
        tryLogin();
    }
}

void SynoSurveillanceStation::tryLogin()
{
    if (apiSid.empty())
    {
        cDebugDom("syno.ss") << "Try to Login to API";

        login([=](const string &sid)
        {
            if (sid.empty())
            {
                cWarning() << "Syno: Fail to login to Synology api";
                authUrl.clear();
                apiSid.clear();
                snapUrl.clear();
                isRunning = false;
                downloadDataCb({});
                return;
            }

            apiSid = sid;
            tryGetSnapshot();
        });
    }
    else
    {
        tryGetSnapshot();
    }
}

void SynoSurveillanceStation::tryGetSnapshot()
{
    if (snapUrl.empty())
    {
        cDebugDom("syno.ss") << "Probing API info for GetSnapshot";
        getApiInfo("SYNO.SurveillanceStation.Camera", "GetSnapshot", "9", [=](const string &api)
        {
            if (api.empty())
            {
                cWarning() << "Syno: Fail to get api info for SYNO.SurveillanceStation.Camera";
                authUrl.clear();
                apiSid.clear();
                snapUrl.clear();
                isRunning = false;
                downloadDataCb({});
                return;
            }

            snapUrl = api;
            doGetSnapshot();
        });
    }
    else
    {
        doGetSnapshot();
    }
}

void SynoSurveillanceStation::doGetSnapshot()
{
    getSnapshot([=](const string &data)
    {
        if (data.empty())
        {
            authUrl.clear();
            apiSid.clear();
            snapUrl.clear();
        }
        lastSnapshot = data;
        cInfo() << "new snapshot: " << lastSnapshot.length();
        isRunning = false;
        downloadDataCb(lastSnapshot);
    });
}

void SynoSurveillanceStation::getSnapshot(std::function<void(const string &data)> cb)
{
    cDebugDom("syno.ss") << "Getting new Snapshot...";
    string url = snapUrl;
    url += "&id=" + Utils::url_encode(get_param("camera_id"));
    url += "&_sid=" + Utils::url_encode(apiSid);
    if (!get_param("camera_profile").empty())
        url += "&profileType=" + Utils::url_encode(get_param("camera_profile"));

    UrlDownloader *dl = new UrlDownloader(url, true);

    dl->m_signalCompleteData.connect([=](const string &data, int status)
    {
        cDebugDom("syno.ss") << "getSnapshot done, status: " << status;

        if (status == 200)
        {
            auto headers = dl->getResponseHeaders();
            cDebugDom("syno.ss") << "Headers Content-Type: " << headers["Content-Type"];
            if (headers["Content-Type"] != "image/jpeg")
            {
                cWarning() << "GetSnapshot failed: " << data;
                cb({});
            }

            cb(data);
        }
        else
        {
            cWarning() << "HTTP error: " << status;
            cb({});
        }
    });
    dl->httpGet();
}

void SynoSurveillanceStation::login(std::function<void(const string &sid)> cb)
{
    string url = authUrl;
    url += "&format=2";
    url += "&account=" + Utils::url_encode(get_param("username"));
    url += "&passwd=" + Utils::url_encode(get_param("password"));

    UrlDownloader *dl = new UrlDownloader(url, true);

    dl->m_signalCompleteData.connect([=](const string &data, int status)
    {
        if (status == 200)
        {
            bool err;
            Json jdata = parseJsonResult(data, err);

            if (err || !jdata["sid"].is_string())
            {
                cb({});
                return;
            }

            cb(jdata["sid"]);
        }
        else
        {
            cWarning() << "HTTP error: " << status;
            cb({});
        }
    });
    dl->httpGet();
}

void SynoSurveillanceStation::getApiInfo(const string &api, const string &method, const string &version,
                                         std::function<void(const string &url)> cb)
{
    string url = SynoApiInfo;
    Utils::replace_str(url, "%1", get_param("url"));
    Utils::replace_str(url, "%2", api);
    UrlDownloader *dl = new UrlDownloader(url, true);

    dl->m_signalCompleteData.connect([=](const string &data, int status)
    {
        if (status == 200)
        {
            bool err;
            Json jdata = parseJsonResult(data, err);

            if (err || !jdata[api].is_object())
            {
                cb({});
                return;
            }

            Json japi = jdata[api];

            string u = SynoApi;
            Utils::replace_str(u, "%1", get_param("url"));
            Utils::replace_str(u, "%2", japi["path"]);
            Utils::replace_str(u, "%3", api);
            Utils::replace_str(u, "%4", method);
            Utils::replace_str(u, "%5", version);

            cDebugDom("syno.ss") << "API URL is: " << u;

            cb({u});
        }
        else
        {
            cWarning() << "HTTP error: " << status;
            cb({});
        }
    });
    dl->httpGet();
}

Json SynoSurveillanceStation::parseJsonResult(const string &data, bool &error)
{
    Json jdoc;
    try
    {
        error = false;
        jdoc = Json::parse(data);
        if (!jdoc.is_object())
            throw (invalid_argument(string("Json is not an object")));

        if (!jdoc["success"])
            throw (invalid_argument(string("success is false")));

        Json jdata = jdoc["data"];
        if (!jdata.is_object())
            throw (invalid_argument(string("Json is not an object")));

        return jdata;
    }
    catch (const std::exception &e)
    {
        cWarning() << "Syno: Error parsing '" << data << "':" << e.what();
        error = true;
        return {};
    }
}
