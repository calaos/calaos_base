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
#ifndef S_ACTIONCAMERADOWNLOAD_H
#define S_ACTIONCAMERADOWNLOAD_H

#include "Calaos.h"
#include "ListeRoom.h"
#include "IPCam.h"
#include "UrlDownloader.h"

namespace Calaos
{

/*
 * Camera snapshot download, shared by ActionMail and ActionPush.
 *
 * The download runs asynchronously: UrlDownloader spawns curl and emits
 * m_signalComplete from the event loop, then deletes itself through an Idler.
 * The action that started it can be destroyed at any point in between (config
 * reload, IO deletion), so a completion slot capturing the action's `this`
 * would dereference freed memory.
 *
 * The slot is therefore owned by this object, which actions keep as a member:
 * destroying the action destroys the guard, the guard disconnects the slot, and
 * the callback can never run on a dead action.
 *
 * The transfer itself is not aborted. UrlDownloader has no cancel API, and
 * deleting it from the outside would race with the autodelete Idler it queues
 * on completion (double delete), so the transfer is left to finish and free
 * itself; only the callback into the action is severed.
 */
class ActionCameraDownload
{
public:
    ActionCameraDownload() = default;
    ~ActionCameraDownload() { cancel(); }

    ActionCameraDownload(const ActionCameraDownload &) = delete;
    ActionCameraDownload &operator =(const ActionCameraDownload &) = delete;

    //Returns the camera IO named ioId, or nullptr when ioId is empty or is not a camera
    static IPCam *findCamera(const string &ioId)
    {
        if (ioId.empty())
            return nullptr;
        return dynamic_cast<IPCam *>(ListeRoom::Instance().get_io(ioId));
    }

    /* Downloads the current picture of camera into destFile.
     * cb(success) is called when the transfer completes, success being true
     * when the http status is in the 2xx band.
     * Returns false when the transfer could not even be started, cb is then
     * never called.
     */
    bool start(IPCam *camera, const string &destFile, std::function<void(bool)> cb)
    {
        if (!camera)
            return false;

        cInfoDom("rule.action") << "Need to download camera ("
                                << camera->get_param("name")
                                << ") attachment";
        cDebugDom("rule.action") << "DL URL: " << camera->getPictureUrl()
                                 << " to " << destFile;

        //Never keep more than one pending download per action
        cancel();

        UrlDownloader *dl = new UrlDownloader(camera->getPictureUrl(), true);
        m_conn = dl->m_signalComplete.connect([cb](int status)
        {
            //http success band is [200, 300)
            cb(status >= 200 && status < 300);
        });

        if (!dl->httpGet(destFile))
        {
            cWarningDom("rule.action") << "Failed to start camera download for "
                                       << camera->get_param("name");
            cancel();
            dl->Destroy();
            return false;
        }

        return true;
    }

    //Severs the pending completion callback, if any
    void cancel() { m_conn.disconnect(); }

private:
    sigc::connection m_conn;
};

}
#endif
