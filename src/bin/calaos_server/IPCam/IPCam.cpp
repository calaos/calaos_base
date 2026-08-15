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
#include "IPCam.h"
#include "ListeRoom.h"

using namespace Calaos;

IPCam::IPCam(Params &p):
    IOBase(p, IOBase::IO_INOUT)
{
    if (!param_exists("port"))
        set_param("port", "80");

    set_param("gui_type", "camera");
    set_param("visible", "false");

    ioDoc->paramAdd("width", _("Width of the image, if this parameter is set, video will be resized to fit the given width. Let parameter empty to keep the original size."), IODoc::TYPE_INT, false, "");
    ioDoc->paramAdd("rotate", _("Rotate the image. Set a value between. The value is in degrees. Example : -90 for  Counter Clock Wise rotation, 90 for Clock Wise rotationCW."), IODoc::TYPE_INT, false, "");
    //T2.19: insecure by default (user decision) — most cameras serve
    //self-signed HTTPS, existing configs have no insecure param
    ioDoc->paramAdd("insecure", _("Skip TLS certificate verification when connecting to the camera. Default to true (most cameras use self-signed certificates). Set to false to only allow a verified HTTPS connection."), IODoc::TYPE_BOOL, false, "true");

    //Add again to cache, because gui_type has changed
    //Special case for Camera
    ListeRoom::Instance().addIOHash(this);
}

IPCam::~IPCam()
{
    delete cameraSnapDl;
}

bool IPCam::set_value(std::string val)
{
    if (!isEnabled()) return true;

    cInfoDom("output") << "CamOutput(" << get_param("id") << "): got action, " << val;

    if (val.compare(0, 5, "move ") == 0)
    {
        val.erase(0, 5);
        activateCapabilities("ptz", "move", val);
    }
    else if (val.compare(0, 5, "save ") == 0)
    {
        val.erase(0, 5);
        activateCapabilities("position", "save", val);
    }
    else if (val.compare(0, 7, "recall ") == 0)
    {
        val.erase(0, 7);
        activateCapabilities("position", "recall", val);
    }

    EventManager::create(CalaosEvent::EventIOChanged,
                         { { "id", get_param("id") },
                           { "state", val } });

    return true;
}

bool IPCam::SaveToXml(TiXmlElement *node)
{
    TiXmlElement *cnode = new TiXmlElement("calaos:camera");
    node->LinkEndChild(cnode);

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        param.get_item(i, key, value);
        cnode->SetAttribute(key, value);
    }

    return true;
}

std::string IPCam::maskUrlCredentials(const std::string &url)
{
    //T2.17: implementation moved to src/lib (Utils::maskUrlCredentials) so
    //UrlDownloader can mask the URLs it logs too; contract unchanged
    return Utils::maskUrlCredentials(url);
}

void IPCam::downloadSnapshot(std::function<void(const string &)> dataCb)
{
    if (!cameraSnapDl)
    {
        cameraSnapDl = new UrlDownloader(getPictureUrl(), false);
        //T2.19: insecure unless the device opts in to hardening with
        //insecure="false" (local cameras commonly serve self-signed HTTPS)
        cameraSnapDl->setInsecureFromParam(get_param("insecure"));
        cameraSnapDl->m_signalCompleteData.connect([=](const string &downloadedData, int status)
        {
            lastSnapshot = downloadedData;
            if (status == 200)
            {
                snapshotDataCb(downloadedData);
            }
            else
            {
                cErrorDom("network") << "Failed to get image for camera at url: " << maskUrlCredentials(getPictureUrl()) << " failed with code: " << status;
                snapshotDataCb({});
            }
        });
    }

    if (cameraSnapDl->isRunning())
    {
        Timer::singleShot(0, [=]() { dataCb(lastSnapshot); });
        return;
    }

    snapshotDataCb = dataCb;
    cameraSnapDl->httpGet();
}
