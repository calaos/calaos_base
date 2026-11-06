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
#include "PictureResizer.h"

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

bool IPCam::SaveToXml(pugi::xml_node node)
{
    pugi::xml_node cnode = node.append_child("calaos:camera");

    for (int i = 0;i < get_params().size();i++)
    {
        string key, value;
        param.get_item(i, key, value);
        XmlUtils::setAttribute(cnode, key, value);
    }

    return true;
}

std::string IPCam::maskUrlCredentials(const std::string &url)
{
    //Kept as the masker it always was, but no log line uses it any more: it
    //knows the userinfo and ten query names and cannot see a secret carried
    //by a path segment. What goes to a journal goes through Utils::urlForLog.
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
        //Polled by clients: the host of a camera does not go to the journal
        cameraSnapDl->setQuiet(true);
        cameraSnapDl->m_signalCompleteData.connect([=](const string &downloadedData, int status)
                                                   {
            if (status == 200)
            {
                finishSnapshot(downloadedData);
            }
            else
            {
                cErrorDom("network") << "Failed to get camera image, status: " << status;
                finishSnapshot({});
            }
        });
    }

    snapshotWaiters.push_back(std::move(dataCb));

    if (cameraSnapDl->isRunning())
        return;

    if (!cameraSnapDl->httpGet())
        finishSnapshot({});
}

void IPCam::finishSnapshot(const string &data)
{
    //A waiter may start the next download, which clears the buffer behind data
    string frame = data;
    auto waiters = std::move(snapshotWaiters);
    snapshotWaiters.clear();

    for (auto &waiter: waiters)
        waiter(frame);
}

void IPCam::downloadThumbnail(int width, std::function<void(const string &)> dataCb)
{
    auto waiting = thumbnailWaiters.find(width);
    if (waiting != thumbnailWaiters.end())
    {
        waiting->second.push_back(std::move(dataCb));
        return;
    }

    if (thumbnailWaiters.size() >= maxThumbnailWidths)
    {
        PictureResizer::Instance().reportFallback(PictureResizer::Fallback::Busy);
        downloadSnapshot(std::move(dataCb));
        return;
    }

    thumbnailWaiters[width].push_back(std::move(dataCb));

    downloadSnapshot([this, alive = ioAlive.token(), width](const string &data)
                     {
        if (alive.expired()) return;
        resizeSnapshot(width, data);
    });
}

void IPCam::resizeSnapshot(int width, const string &data)
{
    if (data.empty())
    {
        finishThumbnail(width, data);
        return;
    }

    auto &resizer = PictureResizer::Instance();
    auto source = std::make_shared<const string>(data);

    bool accepted = resizer.submit(source, width,
                                   [this, alive = ioAlive.token(), width, source](JpegThumbnail::Result result)
                                   {
        if (alive.expired()) return;

        auto &resizer = PictureResizer::Instance();
        switch (result.status)
        {
        case JpegThumbnail::Status::Resized:
            finishThumbnail(width, result.data);
            return;
        case JpegThumbnail::Status::Unchanged:
            break;
        case JpegThumbnail::Status::Failed:
            resizer.reportFallback(PictureResizer::Fallback::Undecodable);
            break;
        case JpegThumbnail::Status::Unavailable:
            resizer.reportFallback(PictureResizer::Fallback::NoDecoder);
            break;
        }
        finishThumbnail(width, *source);
    });

    if (!accepted)
    {
        resizer.reportFallback(PictureResizer::Fallback::Busy);
        finishThumbnail(width, *source);
    }
}

void IPCam::finishThumbnail(int width, const string &data)
{
    auto waiting = thumbnailWaiters.find(width);
    auto waiters = std::move(waiting->second);
    thumbnailWaiters.erase(waiting);

    for (auto &waiter: waiters)
        waiter(data);
}
